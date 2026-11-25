"""시험용 임시 PostgreSQL 클러스터 (REQ-LOG-001·003 통합 시험).

임시 디렉터리에 initdb로 클러스터를 만들고 유닉스 소켓으로만 띄운다(TCP 포트를 열지 않음).
root로 실행하면 initdb·postgres가 거부하므로 `postgres` 사용자로 실행한다.
PostgreSQL 서버 프로그램이 없으면 `unavailable_reason()`이 사유를 돌려주고, 시험은 그 사유로 skip한다.

조회는 psql(-At)로 한다. Python DB 드라이버를 시험 의존성에 더하지 않기 위해서다.
"""
import glob
import os
import pwd
import shutil
import signal
import subprocess
import tempfile
from pathlib import Path

PORT = 5432  # 유닉스 소켓 파일 이름에만 쓰인다 (.s.PGSQL.5432). 소켓 디렉터리가 시험마다 달라 충돌하지 않는다


def _bin_dir() -> Path | None:
    if env := os.environ.get("UGVC_PG_BIN"):
        return Path(env)
    found = sorted(glob.glob("/usr/lib/postgresql/*/bin/initdb"))
    return Path(found[-1]).parent if found else None


def _run_as() -> str | None:
    """root면 postgres 사용자로 실행한다. 그 밖에는 현재 사용자로."""
    return "postgres" if os.geteuid() == 0 else None


def unavailable_reason() -> str | None:
    bin_dir = _bin_dir()
    if bin_dir is None or not (bin_dir / "initdb").exists():
        return "PostgreSQL 서버(initdb)가 없음: postgresql-16 설치 또는 UGVC_PG_BIN 지정"
    if shutil.which("psql") is None:
        return "psql이 없음: postgresql-client 설치"
    user = _run_as()
    if user is not None:
        try:
            pwd.getpwnam(user)
        except KeyError:
            return "root로 실행 중인데 postgres 사용자가 없음 (initdb는 root로 실행할 수 없다)"
    return None


class PgCluster:
    def __init__(self) -> None:
        self.bin = _bin_dir()
        self.user = _run_as()
        self.dir = Path(tempfile.mkdtemp(prefix="ugvc-pg-"))
        self.data = self.dir / "data"
        self.sock = self.dir  # 소켓 디렉터리
        if self.user is not None:
            pw = pwd.getpwnam(self.user)
            os.chown(self.dir, pw.pw_uid, pw.pw_gid)
        self._db_seq = 0

    def _cmd(self, *args: str) -> list[str]:
        prefix = ["runuser", "-u", self.user, "--"] if self.user else []
        return [*prefix, *args]

    def init(self) -> None:
        subprocess.run(self._cmd(str(self.bin / "initdb"), "-D", str(self.data), "-A", "trust", "-U", "postgres",
                                 "--no-sync", "-E", "UTF8"),
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def start(self) -> None:
        opts = f"-k {self.sock} -p {PORT} -c listen_addresses='' -c fsync=off"
        subprocess.run(self._cmd(str(self.bin / "pg_ctl"), "-D", str(self.data), "-o", opts,
                                 "-l", str(self.dir / "server.log"), "-w", "start"),
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def stop(self, mode: str = "fast") -> None:
        subprocess.run(self._cmd(str(self.bin / "pg_ctl"), "-D", str(self.data), "-m", mode, "-w", "stop"),
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def destroy(self) -> None:
        self.stop("immediate")
        shutil.rmtree(self.dir, ignore_errors=True)

    def conninfo(self, dbname: str) -> str:
        return f"host={self.sock} port={PORT} user=postgres dbname={dbname}"

    def psql(self, sql: str, dbname: str = "postgres") -> list[list[str]]:
        out = subprocess.run(["psql", "-X", "-At", "-F", "\t", "-v", "ON_ERROR_STOP=1",
                              "-h", str(self.sock), "-p", str(PORT), "-U", "postgres", "-d", dbname, "-c", sql],
                             check=True, capture_output=True, text=True).stdout
        return [line.split("\t") for line in out.splitlines()]

    def new_database(self) -> str:
        """시험마다 빈 DB 하나. GW가 접속하면서 스키마를 만든다."""
        self._db_seq += 1
        name = f"ugvc_t{self._db_seq}"
        self.psql(f"CREATE DATABASE {name}")
        return name

    def gateway_backend_pids(self) -> list[int]:
        """GW(application_name=ugvc_gateway)를 맡은 서버 프로세스."""
        rows = self.psql("SELECT pid FROM pg_stat_activity WHERE application_name = 'ugvc_gateway'")
        return [int(r[0]) for r in rows]

    def freeze(self, pids: list[int]) -> None:
        """서버 프로세스를 멈춘다(SIGSTOP): 응답하지 않는 DB를 재현한다."""
        for pid in pids:
            os.kill(pid, signal.SIGSTOP)

    def thaw(self, pids: list[int]) -> None:
        for pid in pids:
            os.kill(pid, signal.SIGCONT)
