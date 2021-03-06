/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <gtest/gtest.h>

#include <thread>

#include <folly/Baton.h>
#include <folly/experimental/observer/SimpleObservable.h>

using namespace folly::observer;

TEST(Observer, Observable) {
  SimpleObservable<int> observable(42);
  auto observer = observable.getObserver();

  EXPECT_EQ(42, **observer);

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([observer, &baton]() {
    *observer;
    baton.post();
    return folly::Unit();
  });
  baton.reset();

  observable.setValue(24);

  EXPECT_TRUE(baton.timed_wait(std::chrono::seconds{1}));

  EXPECT_EQ(24, **observer);
}

TEST(Observer, MakeObserver) {
  SimpleObservable<int> observable(42);

  auto observer = makeObserver([child = observable.getObserver()]() {
    return **child + 1;
  });

  EXPECT_EQ(43, **observer);

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([observer, &baton]() {
    *observer;
    baton.post();
    return folly::Unit();
  });
  baton.reset();

  observable.setValue(24);

  EXPECT_TRUE(baton.timed_wait(std::chrono::seconds{1}));

  EXPECT_EQ(25, **observer);
}

TEST(Observer, MakeObserverDiamond) {
  SimpleObservable<int> observable(42);

  auto observer1 = makeObserver([child = observable.getObserver()]() {
    return **child + 1;
  });

  auto observer2 = makeObserver([child = observable.getObserver()]() {
    return std::make_shared<int>(**child + 2);
  });

  auto observer = makeObserver(
      [observer1, observer2]() { return (**observer1) * (**observer2); });

  EXPECT_EQ(43 * 44, *observer.getSnapshot());

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([observer, &baton]() {
    *observer;
    baton.post();
    return folly::Unit();
  });
  baton.reset();

  observable.setValue(24);

  EXPECT_TRUE(baton.timed_wait(std::chrono::seconds{1}));

  EXPECT_EQ(25 * 26, **observer);
}

TEST(Observer, CreateException) {
  struct ExpectedException {};
  EXPECT_THROW(
      auto observer = makeObserver(
          []() -> std::shared_ptr<int> { throw ExpectedException(); }),
      ExpectedException);

  EXPECT_THROW(
      auto observer =
          makeObserver([]() -> std::shared_ptr<int> { return nullptr; }),
      std::logic_error);
}

TEST(Observer, NullValue) {
  SimpleObservable<int> observable(41);
  auto oddObserver = makeObserver([innerObserver = observable.getObserver()]() {
    auto value = **innerObserver;

    if (value % 2 != 0) {
      return value * 2;
    }

    throw std::logic_error("I prefer odd numbers");
  });

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([oddObserver, &baton]() {
    *oddObserver;
    baton.post();
    return folly::Unit();
  });

  baton.reset();
  EXPECT_EQ(82, **oddObserver);

  observable.setValue(2);

  // Waiting observer shouldn't be updated
  EXPECT_FALSE(baton.timed_wait(std::chrono::seconds{1}));
  baton.reset();

  EXPECT_EQ(82, **oddObserver);

  observable.setValue(23);

  EXPECT_TRUE(baton.timed_wait(std::chrono::seconds{1}));

  EXPECT_EQ(46, **oddObserver);
}

TEST(Observer, Stress) {
  SimpleObservable<int> observable(0);

  folly::Synchronized<std::vector<int>> values;

  auto observer = makeObserver([ child = observable.getObserver(), &values ]() {
    auto value = **child * 10;
    values.withWLock(
        [&](std::vector<int>& values) { values.push_back(value); });
    return value;
  });

  EXPECT_EQ(0, **observer);
  values.withRLock([](const std::vector<int>& values) {
    EXPECT_EQ(1, values.size());
    EXPECT_EQ(0, values.back());
  });

  constexpr size_t numIters = 10000;

  for (size_t i = 1; i <= numIters; ++i) {
    observable.setValue(i);
  }

  while (**observer != numIters * 10) {
    std::this_thread::yield();
  }

  values.withRLock([numIters = numIters](const std::vector<int>& values) {
    EXPECT_EQ(numIters * 10, values.back());
    EXPECT_LT(values.size(), numIters / 2);
    EXPECT_GT(values.size(), 10);

    for (auto value : values) {
      EXPECT_EQ(0, value % 10);
    }

    for (size_t i = 0; i < values.size() - 1; ++i) {
      EXPECT_LE(values[i], values[i + 1]);
    }
  });
}
