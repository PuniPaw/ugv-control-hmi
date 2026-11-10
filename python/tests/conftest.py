import json
from pathlib import Path

import pytest

VECTORS_DIR = Path(__file__).resolve().parents[2] / "testdata" / "vectors"


@pytest.fixture(scope="session")
def frame_vectors() -> dict:
    return json.loads((VECTORS_DIR / "frame.json").read_text(encoding="utf-8"))
