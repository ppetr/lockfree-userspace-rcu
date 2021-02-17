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

#include <cstring>
#include <iostream>
#include <string>

#include "refcount_struct.h"

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

class Bar {
  refptr::Ref<Foo> bar1;
  refptr::Ref<const Foo> bar2;
};

}  // namespace

int main() {
  using refptr::New;
  using refptr::Ref;

  int counter = 0;
  {
    Ref<Foo> owned(
        New<Foo, int&, const char*>(16, counter, "Lorem ipsum dolor sit amet"));
    assert(counter == 1);
    std::cout << owned->text() << std::endl;
    Ref<const Foo> shared(std::move(owned).Share());
    assert(counter == 1);
    std::cout << shared->text() << std::endl;
#ifdef __cpp_lib_optional
    auto owned_opt = std::move(shared).AttemptToClaim();
    assert(counter == 1);
    assert(("Attempt to claim ownership failed", owned_opt));
    owned = *std::move(owned_opt);
    assert(counter == 1);
    std::cout << owned->text() << std::endl;
#endif
  }
  assert(counter == 0);
  {
    void* deleter_arg;
    {
      Ref<Foo> owned(New<Foo, int&, const char*>(16, counter,
                                                 "Lorem ipsum dolor sit amet"));
      assert(counter == 1);
      deleter_arg = owned.ToDeleterArg();
    }
    assert(counter == 1);
    Ref<Foo>::Deleter(deleter_arg);
  }
  assert(counter == 0);
  return 0;
}
