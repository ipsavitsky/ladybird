/*
 * Copyright (c) 2022-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Optional.h>
#include <LibWeb/Loader/UserAgent.h>
#include <LibWeb/WebDriver/Capabilities.h>
#include <LibWeb/WebDriver/Proxy.h>
#include <LibWeb/WebDriver/TimeoutsConfiguration.h>
#include <LibWeb/WebDriver/UserPrompt.h>

namespace Web::WebDriver {

// https://w3c.github.io/webdriver/#dfn-deserialize-as-a-page-load-strategy
static Response deserialize_as_a_page_load_strategy(JsonValue value)
{
    // 1. If value is not a string return an error with error code invalid argument.
    if (!value.is_string())
        return Error::from_code(ErrorCode::InvalidArgument, "Capability pageLoadStrategy must be a string"sv);

    // 2. If there is no entry in the table of page load strategies with keyword value return an error with error code invalid argument.
    if (!value.as_string().is_one_of("none"sv, "eager"sv, "normal"sv))
        return Error::from_code(ErrorCode::InvalidArgument, "Invalid pageLoadStrategy capability"sv);

    // 3. Return success with data value.
    return value;
}

static InterfaceMode default_interface_mode { InterfaceMode::Graphical };

void set_default_interface_mode(InterfaceMode interface_mode)
{
    default_interface_mode = interface_mode;
}

static Response deserialize_as_ladybird_capability(StringView name, JsonValue value)
{
    if (name == "ladybird:headless"sv) {
        if (!value.is_bool())
            return Error::from_code(ErrorCode::InvalidArgument, "Extension capability ladybird:headless must be a boolean"sv);
    }

    return value;
}

static void set_default_ladybird_capabilities(JsonObject& options)
{
    options.set("ladybird:headless"sv, default_interface_mode == InterfaceMode::Headless);
}

// https://w3c.github.io/webdriver/#dfn-validate-capabilities
static ErrorOr<JsonObject, Error> validate_capabilities(JsonValue const& capability)
{
    // 1. If capability is not a JSON Object return an error with error code invalid argument.
    if (!capability.is_object())
        return Error::from_code(ErrorCode::InvalidArgument, "Capability is not an Object"sv);

    // 2. Let result be an empty JSON Object.
    JsonObject result;

    // 3. For each enumerable own property in capability, run the following substeps:
    TRY(capability.as_object().try_for_each_member([&](auto const& name, JsonValue const& value) -> ErrorOr<void, Error> {
        // a. Let name be the name of the property.
        // b. Let value be the result of getting a property named name from capability.

        // c. Run the substeps of the first matching condition:
        JsonValue deserialized;

        // -> value is null
        if (value.is_null()) {
            // Let deserialized be set to null.
        }

        // -> name equals "acceptInsecureCerts"
        else if (name == "acceptInsecureCerts"sv) {
            // If value is not a boolean return an error with error code invalid argument. Otherwise, let deserialized be set to value
            if (!value.is_bool())
                return Error::from_code(ErrorCode::InvalidArgument, "Capability acceptInsecureCerts must be a boolean"sv);
            deserialized = value;
        }

        // -> name equals "browserName"
        // -> name equals "browserVersion"
        // -> name equals "platformName"
        else if (name.is_one_of("browserName"sv, "browserVersion"sv, "platformName"sv)) {
            // If value is not a string return an error with error code invalid argument. Otherwise, let deserialized be set to value.
            if (!value.is_string())
                return Error::from_code(ErrorCode::InvalidArgument, ByteString::formatted("Capability {} must be a string", name));
            deserialized = value;
        }

        // -> name equals "pageLoadStrategy"
        else if (name == "pageLoadStrategy"sv) {
            // Let deserialized be the result of trying to deserialize as a page load strategy with argument value.
            deserialized = TRY(deserialize_as_a_page_load_strategy(value));
        }

        // -> name equals "proxy"
        else if (name == "proxy"sv) {
            // Let deserialized be the result of trying to deserialize as a proxy with argument value.
            deserialized = TRY(deserialize_as_a_proxy(value));
        }

        // -> name equals "strictFileInteractability"
        else if (name == "strictFileInteractability"sv) {
            // If value is not a boolean return an error with error code invalid argument. Otherwise, let deserialized be set to value
            if (!value.is_bool())
                return Error::from_code(ErrorCode::InvalidArgument, "Capability strictFileInteractability must be a boolean"sv);
            deserialized = value;
        }

        // -> name equals "timeouts"
        else if (name == "timeouts"sv) {
            // Let deserialized be the result of trying to JSON deserialize as a timeouts configuration the value.
            auto timeouts = TRY(json_deserialize_as_a_timeouts_configuration(value));
            deserialized = JsonValue { timeouts_object(timeouts) };
        }

        // -> name equals "unhandledPromptBehavior"
        else if (name == "unhandledPromptBehavior"sv) {
            // Let deserialized be the result of trying to deserialize as an unhandled prompt behavior with argument value.
            deserialized = TRY(deserialize_as_an_unhandled_prompt_behavior(value));
        }

        // FIXME: -> name is the name of an additional WebDriver capability
        // FIXME:     Let deserialized be the result of trying to run the additional capability deserialization algorithm for the extension capability corresponding to name, with argument value.

        // https://w3c.github.io/webdriver-bidi/#type-session-CapabilityRequest
        else if (name == "webSocketUrl"sv) {
            // 1. If value is not a boolean, return error with code invalid argument.
            if (!value.is_bool())
                return Error::from_code(ErrorCode::InvalidArgument, "Capability webSocketUrl must be a boolean"sv);

            // 2. Return success with data value.
            deserialized = value;
        }

        // -> name is the key of an extension capability
        else if (name.contains(':')) {
            // If name is known to the implementation, let deserialized be the result of trying to deserialize value in
            // an implementation-specific way. Otherwise, let deserialized be set to value.
            if (name.starts_with("ladybird:"sv))
                deserialized = TRY(deserialize_as_ladybird_capability(name, value));
        }

        // -> The remote end is an endpoint node
        else {
            // Return an error with error code invalid argument.
            return Error::from_code(ErrorCode::InvalidArgument, ByteString::formatted("Unrecognized capability: {}", name));
        }

        // d. If deserialized is not null, set a property on result with name name and value deserialized.
        if (!deserialized.is_null())
            result.set(name, move(deserialized));

        return {};
    }));

    // 4. Return success with data result.
    return result;
}

// https://w3c.github.io/webdriver/#dfn-merging-capabilities
static ErrorOr<JsonObject, Error> merge_capabilities(JsonObject const& primary, Optional<JsonObject const&> const& secondary)
{
    // 1. Let result be a new JSON Object.
    JsonObject result;

    // 2. For each enumerable own property in primary, run the following substeps:
    primary.for_each_member([&](auto const& name, auto const& value) {
        // a. Let name be the name of the property.
        // b. Let value be the result of getting a property named name from primary.

        // c. Set a property on result with name name and value value.
        result.set(name, value);
    });

    // 3. If secondary is undefined, return result.
    if (!secondary.has_value())
        return result;

    // 4. For each enumerable own property in secondary, run the following substeps:
    TRY(secondary->try_for_each_member([&](auto const& name, auto const& value) -> ErrorOr<void, Error> {
        // a. Let name be the name of the property.
        // b. Let value be the result of getting a property named name from secondary.

        // c. Let primary value be the result of getting the property name from primary.
        auto primary_value = primary.get(name);

        // d. If primary value is not undefined, return an error with error code invalid argument.
        if (primary_value.has_value())
            return Error::from_code(ErrorCode::InvalidArgument, ByteString::formatted("Unable to merge capability {}", name));

        // e. Set a property on result with name name and value value.
        result.set(name, value);
        return {};
    }));

    // 5. Return result.
    return result;
}

static bool matches_browser_version(StringView requested_version, StringView required_version)
{
    // FIXME: Handle relative (>, >=, <. <=) comparisons. For now, require an exact match.
    return requested_version == required_version;
}

static bool matches_platform_name(StringView requested_platform_name, StringView required_platform_name)
{
    if (requested_platform_name == required_platform_name)
        return true;

    // The following platform names are in common usage with well-understood semantics and, when matching capabilities, greatest interoperability can be achieved by honoring them as valid synonyms for well-known Operating Systems:
    //     "linux"   Any server or desktop system based upon the Linux kernel.
    //     "mac"     Any version of Apple’s macOS.
    //     "windows" Any version of Microsoft Windows, including desktop and mobile versions.
    // This list is not exhaustive.

    // NOTE: Of the synonyms listed in the spec, the only one that differs for us is macOS.
    //       Further, we are allowed to handle synonyms for SerenityOS.
    if (requested_platform_name == "mac"sv && required_platform_name == "macos"sv)
        return true;
    if (requested_platform_name == "serenity"sv && required_platform_name == "serenityos"sv)
        return true;
    return false;
}

// https://w3c.github.io/webdriver/#dfn-matching-capabilities
static JsonValue match_capabilities(JsonObject const& capabilities, SessionFlags flags)
{
    static auto browser_name = StringView { BROWSER_NAME, strlen(BROWSER_NAME) }.to_lowercase_string();
    static auto platform_name = StringView { OS_STRING, strlen(OS_STRING) }.to_lowercase_string();

    // 1. Let matched capabilities be a JSON Object with the following entries:
    JsonObject matched_capabilities;
    // "browserName"
    //     ASCII Lowercase name of the user agent as a string.
    matched_capabilities.set("browserName"sv, browser_name);
    // "browserVersion"
    //     The user agent version, as a string.
    matched_capabilities.set("browserVersion"sv, BROWSER_VERSION);
    // "platformName"
    //     ASCII Lowercase name of the current platform as a string.
    matched_capabilities.set("platformName"sv, platform_name);
    // "acceptInsecureCerts"
    //     Boolean initially set to false, indicating the session will not implicitly trust untrusted or self-signed TLS certificates on navigation.
    matched_capabilities.set("acceptInsecureCerts"sv, false);
    // "strictFileInteractability"
    //     Boolean initially set to false, indicating that interactability checks will be applied to <input type=file>.
    // FIXME: Spec issue: This item likely should have been removed in lieu of step 2.
    //        https://github.com/w3c/webdriver/issues/1879
    // "setWindowRect"
    //     Boolean indicating whether the remote end supports all of the resizing and positioning commands.
    matched_capabilities.set("setWindowRect"sv, true);
    // "userAgent"
    //     String containing the default User-Agent value.
    matched_capabilities.set("userAgent"sv, Web::default_user_agent);

    // 2. If flags contains "http", add the following entries to matched capabilities:
    if (has_flag(flags, SessionFlags::Http)) {
        // "strictFileInteractability"
        //     Boolean initially set to false, indicating that interactabilty checks will be applied to <input type=file>.
        matched_capabilities.set("strictFileInteractability"sv, false);
    }

    // 3. Optionally add extension capabilities as entries to matched capabilities. The values of these may be elided,
    //    and there is no requirement that all extension capabilities be added.
    set_default_ladybird_capabilities(matched_capabilities);

    // 3. For each name and value corresponding to capabilities's own properties:
    auto result = capabilities.try_for_each_member([&](auto const& name, auto const& value) -> ErrorOr<void> {
        // a. Let match value equal value.

        // b. Run the substeps of the first matching name:
        // -> "browserName"
        if (name == "browserName"sv) {
            // If value is not a string equal to the "browserName" entry in matched capabilities, return success with data null.
            if (value.as_string() != matched_capabilities.get_byte_string(name).value())
                return AK::Error::from_string_literal("browserName");
        }
        // -> "browserVersion"
        else if (name == "browserVersion"sv) {
            // Compare value to the "browserVersion" entry in matched capabilities using an implementation-defined comparison algorithm. The comparison is to accept a value that places constraints on the version using the "<", "<=", ">", and ">=" operators.
            // If the two values do not match, return success with data null.
            if (!matches_browser_version(value.as_string(), matched_capabilities.get_byte_string(name).value()))
                return AK::Error::from_string_literal("browserVersion");
        }
        // -> "platformName"
        else if (name == "platformName"sv) {
            // If value is not a string equal to the "platformName" entry in matched capabilities, return success with data null.
            if (!matches_platform_name(value.as_string(), matched_capabilities.get_byte_string(name).value()))
                return AK::Error::from_string_literal("platformName");
        }
        // -> "acceptInsecureCerts"
        else if (name == "acceptInsecureCerts"sv) {
            // If accept insecure TLS flag is set and not equal to value, return success with data null.
            if (value.as_bool())
                return AK::Error::from_string_literal("acceptInsecureCerts");
        }
        // -> "proxy"
        else if (name == "proxy"sv) {
            // If the has proxy configuration flag is set, or if the proxy configuration defined in value is not one that
            // passes the endpoint node's implementation-specific validity checks, return success with data null.
            if (has_proxy_configuration())
                return AK::Error::from_string_literal("proxy");
        }
        // -> "unhandledPromptBehavior"
        else if (name == "unhandledPromptBehavior"sv) {
            // If check user prompt handler matches with value is false, return success with data null.
            if (!check_user_prompt_handler_matches(value.as_object()))
                return AK::Error::from_string_literal("unhandledPromptBehavior");
        }
        // -> Otherwise
        else {
            // FIXME: If name is the name of an additional WebDriver capability which defines a matched capability serialization algorithm, let match value be the result of running the matched capability serialization algorithm for capability name with arguments value, and flags.
            // FIXME: Otherwise, if name is the key of an extension capability, let match value be the result of trying implementation-specific steps to match on name with value. If the match is not successful, return success with data null.

            // https://w3c.github.io/webdriver-bidi/#type-session-CapabilityRequest
            if (name == "webSocketUrl"sv) {
                // 1. If value is false, return success with data null.
                if (!value.as_bool())
                    return AK::Error::from_string_literal("webSocketUrl");

                // 2. Return success with data value.
                // FIXME: Remove this when we support BIDI communication.
                return AK::Error::from_string_literal("webSocketUrl");
            }
        }

        // c. If match value is not null, set a property on matched capabilities with name name and value match value.
        if (!value.is_null())
            matched_capabilities.set(name, value);

        return {};
    });

    if (result.is_error()) {
        dbgln_if(WEBDRIVER_DEBUG, "Failed to match capability: {}", result.error());
        return JsonValue {};
    }

    // 4. Return success with data matched capabilities.
    return matched_capabilities;
}

// https://w3c.github.io/webdriver/#dfn-capabilities-processing
Response process_capabilities(JsonValue const& parameters, SessionFlags flags)
{
    if (!parameters.is_object())
        return Error::from_code(ErrorCode::InvalidArgument, "Session parameters is not an object"sv);

    // 1. Let capabilities request be the result of getting the property "capabilities" from parameters.
    //     a. If capabilities request is not a JSON Object, return error with error code invalid argument.
    auto maybe_capabilities_request = parameters.as_object().get_object("capabilities"sv);
    if (!maybe_capabilities_request.has_value())
        return Error::from_code(ErrorCode::InvalidArgument, "Capabilities is not an object"sv);

    auto const& capabilities_request = maybe_capabilities_request.value();

    // 2. Let required capabilities be the result of getting the property "alwaysMatch" from capabilities request.
    //     a. If required capabilities is undefined, set the value to an empty JSON Object.
    JsonObject required_capabilities;

    if (auto capability = capabilities_request.get("alwaysMatch"sv); capability.has_value()) {
        // b. Let required capabilities be the result of trying to validate capabilities with arguments required capabilities and flag.
        // FIXME: Spec issue: The "flags" parameter should not be provided to validate_capabilities.
        // https://github.com/w3c/webdriver/issues/1879
        required_capabilities = TRY(validate_capabilities(*capability));
    }

    // 3. Let all first match capabilities be the result of getting the property "firstMatch" from capabilities request.
    JsonArray all_first_match_capabilities;

    if (auto capabilities = capabilities_request.get("firstMatch"sv); capabilities.has_value()) {
        // b. If all first match capabilities is not a List with one or more entries, return error with error code invalid argument.
        if (!capabilities->is_array() || capabilities->as_array().is_empty())
            return Error::from_code(ErrorCode::InvalidArgument, "Capability firstMatch must be an array with at least one entry"sv);

        all_first_match_capabilities = capabilities->as_array();
    } else {
        // a. If all first match capabilities is undefined, set the value to a List with a single entry of an empty JSON Object.
        all_first_match_capabilities.must_append(JsonObject {});
    }

    // 4. Let validated first match capabilities be an empty List.
    JsonArray validated_first_match_capabilities;
    validated_first_match_capabilities.ensure_capacity(all_first_match_capabilities.size());

    // 5. For each first match capabilities corresponding to an indexed property in all first match capabilities:
    TRY(all_first_match_capabilities.try_for_each([&](auto const& first_match_capabilities) -> ErrorOr<void, Error> {
        // a. Let validated capabilities be the result of trying to validate capabilities with arguments first match capabilities and flags.
        // FIXME: Spec issue: The "flags" parameter should not be provided to validate_capabilities.
        // https://github.com/w3c/webdriver/issues/1879
        auto validated_capabilities = TRY(validate_capabilities(first_match_capabilities));

        // b. Append validated capabilities to validated first match capabilities.
        validated_first_match_capabilities.must_append(move(validated_capabilities));
        return {};
    }));

    // 6. Let merged capabilities be an empty List.
    JsonArray merged_capabilities;
    merged_capabilities.ensure_capacity(validated_first_match_capabilities.size());

    // 7. For each first match capabilities corresponding to an indexed property in validated first match capabilities:
    TRY(validated_first_match_capabilities.try_for_each([&](auto const& first_match_capabilities) -> ErrorOr<void, Error> {
        // a. Let merged be the result of trying to merge capabilities with required capabilities and first match capabilities as arguments.
        auto merged = TRY(merge_capabilities(required_capabilities, first_match_capabilities.as_object()));

        // b. Append merged to merged capabilities.
        merged_capabilities.must_append(move(merged));
        return {};
    }));

    // 8. For each capabilities corresponding to an indexed property in merged capabilities:
    for (auto const& capabilities : merged_capabilities.values()) {
        // a. Let matched capabilities be the result of trying to match capabilities with capabilities as an argument.
        // FIXME: Spec issue: The "flags" parameter *should* be provided to match_capabilities.
        // https://github.com/w3c/webdriver/issues/1879
        auto matched_capabilities = match_capabilities(capabilities.as_object(), flags);

        // b. If matched capabilities is not null, return success with data matched capabilities.
        if (!matched_capabilities.is_null())
            return matched_capabilities;
    }

    // 9. Return success with data null.
    return JsonValue {};
}

LadybirdOptions::LadybirdOptions(JsonObject const& capabilities)
{
    if (auto headless = capabilities.get_bool("ladybird:headless"sv); headless.has_value())
        this->headless = *headless;
}

}
