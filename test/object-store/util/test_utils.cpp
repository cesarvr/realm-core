////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 Realm Inc.
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

#include <realm/object-store/impl/realm_coordinator.hpp>
#include "test_utils.hpp"

#include <realm/util/base64.hpp>
#include <realm/util/file.hpp>
#include <realm/string_data.hpp>

#include <external/json/json.hpp>

namespace realm {

bool create_dummy_realm(std::string path)
{
    Realm::Config config;
    config.path = path;
    try {
        _impl::RealmCoordinator::get_coordinator(path)->get_realm(config, none);
        REQUIRE_REALM_EXISTS(path);
        return true;
    }
    catch (std::exception&) {
        return false;
    }
}

void reset_test_directory(const std::string& base_path)
{
    util::try_remove_dir_recursive(base_path);
    util::make_dir(base_path);
}

std::vector<char> make_test_encryption_key(const char start)
{
    std::vector<char> vector;
    vector.reserve(64);
    for (int i = 0; i < 64; i++) {
        vector.emplace_back((start + i) % 128);
    }
    return vector;
}

// FIXME: Catch2 limitation on old compilers (currently our android CI)
// https://github.com/catchorg/Catch2/blob/master/docs/limitations.md#clangg----skipping-leaf-sections-after-an-exception
void catch2_ensure_section_run_workaround(bool did_run_a_section, std::string section_name,
                                          std::function<void()> func)
{
    if (did_run_a_section) {
        func();
    }
    else {
        std::cout << "Skipping test section '" << section_name << "' on this run." << std::endl;
    }
}

std::string encode_fake_jwt(const std::string& in)
{
    std::string unencoded_prefix = nlohmann::json({"alg", "HS256"}).dump();
    std::string unencoded_body =
        nlohmann::json(
            {{"user_data", {{"token", in}}}, {"exp", 123}, {"iat", 456}, {"access", {"download", "upload"}}})
            .dump();
    std::string encoded_prefix, encoded_body;
    encoded_prefix.resize(util::base64_encoded_size(unencoded_prefix.size()));
    encoded_body.resize(util::base64_encoded_size(unencoded_body.size()));
    util::base64_encode(unencoded_prefix.data(), unencoded_prefix.size(), &encoded_prefix[0], encoded_prefix.size());
    util::base64_encode(unencoded_body.data(), unencoded_body.size(), &encoded_body[0], encoded_body.size());
    std::string suffix = "Et9HFtf9R3GEMA0IICOfFMVXY7kkTX1wr4qCyhIf58U";
    return encoded_prefix + "." + encoded_body + "." + suffix;
}

} // namespace realm
