// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "var_sized.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {

class Foo {
 public:
  Foo(char* buf, size_t len, int& counter, const char* text)
      : counter_(counter), buf_(buf) {
    strncpy(buf, text, len);
    buf[len - 1] = '\0';
    counter_++;
  }
  virtual ~Foo() {
    counter_--;
    std::cout << "Destructor called" << std::endl;
  }

  const char* text() const { return buf_; }

 private:
  int& counter_;
  const char* buf_;
};

}  // namespace

int main() {
  using refptr::MakeRefCounted;
  using refptr::MakeUnique;

  int counter = 0;
  {
    auto owned = MakeUnique<Foo, char, int&, const char*>(
        16, counter, "Lorem ipsum dolor sit amet");
    assert(counter == 1);
    std::cout << owned->text() << std::endl;
    std::shared_ptr<Foo> shared(std::move(owned));
    assert(!owned);
    assert(counter == 1);
    std::cout << shared->text() << std::endl;
  }
  assert(counter == 0);
  {
    auto owned = MakeRefCounted<Foo, char, int&, const char*>(
        16, counter, "Lorem ipsum dolor sit amet");
    assert(counter == 1);
    std::cout << owned->text() << std::endl;
    auto shared = std::move(owned).Share();
    assert(counter == 1);
    std::cout << shared->text() << std::endl;
  }
  assert(counter == 0);
}
