#pragma once

// std::visit에 타입별 람다를 묶어 넘기는 C++17 관용구.
//   std::visit(overloaded{[](const A&) {...}, [](const B&) {...}}, v);
// 각 람다의 operator()를 상속해 오버로드 집합을 만든다. 처리하지 않은 타입이 있으면 컴파일 오류가 난다.

namespace ugvc::core {

template <typename... Fs>
struct overloaded : Fs... {
  using Fs::operator()...;
};
template <typename... Fs>
overloaded(Fs...) -> overloaded<Fs...>;  // C++17 추론 가이드 (C++20부터는 필요 없음)

}  // namespace ugvc::core
