////////////////////////////////////////////////////////////////////////////
//
// Copyright 2020 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include <realm/object-store/util/scheduler.hpp>

#if REALM_USE_UV
#include <realm/object-store/util/uv/scheduler.hpp>
#elif REALM_USE_CF
#include <realm/object-store/util/apple/scheduler.hpp>
#elif REALM_USE_ALOOPER
#include <realm/object-store/util/android/scheduler.hpp>
#else
#include <realm/object-store/util/generic/scheduler.hpp>
#endif

namespace {
using namespace realm;

class FrozenScheduler : public util::Scheduler {
public:
    void notify() override {}
    void set_notify_callback(std::function<void()>) override {}
    bool is_on_thread() const noexcept override
    {
        return true;
    }
    bool can_deliver_notifications() const noexcept override
    {
        return false;
    }
};
} // anonymous namespace

namespace realm {
namespace util {
Scheduler::~Scheduler() = default;

std::shared_ptr<Scheduler> Scheduler::get_frozen()
{
    static auto s_shared = std::make_shared<FrozenScheduler>();
    return s_shared;
}
} // namespace util
} // namespace realm
