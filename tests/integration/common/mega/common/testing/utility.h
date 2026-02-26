#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/date_time_forward.h>
#include <mega/common/error_or_forward.h>
#include <mega/common/testing/client_forward.h>
#include <mega/common/testing/path_forward.h>
#include <mega/common/type_traits.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <stdfs.h>
#include <string>
#include <thread>
#include <type_traits>

namespace mega
{

struct FileFingerprint;

namespace common
{
namespace testing
{

template<typename Container, typename Predicate>
bool allOf(Container&& container, Predicate predicate)
{
    return std::all_of(std::begin(container), std::end(container), std::move(predicate));
}

template<typename Container, typename Predicate>
bool anyOf(Container&& container, Predicate predicate)
{
    return std::any_of(std::begin(container), std::end(container), std::move(predicate));
}

template<typename Client, typename Period, typename Rep>
auto befriend(Client& client0, Client& client1, std::chrono::duration<Rep, Period> timeout)
    -> std::enable_if_t<std::is_base_of_v<testing::Client, Client>, Error>
{
    // Both clients should be logged in.
    if (client0.loggedIn() != FULLACCOUNT)
        return API_EARGS;

    if (client1.loggedIn() != FULLACCOUNT)
        return API_EARGS;

    auto email0 = client0.email();
    auto email1 = client1.email();

    // The clients shouldn't be logged in as the same user.
    if (email0 == email1)
        return API_EARGS;

    auto contact0 = client0.contact(email1);
    auto contact1 = client1.contact(email0);

    // The users aren't friends.
    if (!contact0 && !contact1)
    {
        // Try and send a friend invitation.
        auto invited = client0.invite(email1);

        // Couldn't send the friend invite.
        if (!invited)
            return invited.error();

        // Wait for our invitation to be received.
        auto invite = waitFor(
            [&]()
            {
                return client1.invited(email0);
            },
            timeout,
            nullptr);

        // Invite wasn't received.
        if (!invite)
            return LOCAL_ETIMEOUT;

        // Try and accept the invitation.
        auto accepted = invite->accept();

        // Couldn't accept the invitation.
        if (accepted != API_OK)
            return accepted;

        // Wait for friendship to be confirmed.
        auto confirmed = waitFor(
            [&]()
            {
                contact0 = client0.contact(email1);
                contact1 = client1.contact(email0);

                return contact0 && contact1;
            },
            timeout);

        // Couldn't confirm friendship.
        if (!confirmed)
            return LOCAL_ETIMEOUT;
    }

    // Contacts should be visible at this point.
    assert(contact0);
    assert(contact1);

    // Verifies a contact, if necessary.
    auto verify = [&](auto& contact)
    {
        // Try and verify the contact.
        auto result = contact.verify();

        // Couldn't verify the contact.
        if (result != API_OK)
            return result;

        // Wait for verification to complete.
        auto verified = waitFor(
            [&]()
            {
                return contact.verified();
            },
            timeout);

        // Verification timed out.
        if (!verified)
            return Error(LOCAL_ETIMEOUT);

        // Verification's complete.
        return Error(API_OK);
    }; // verify

    // Try and verify friendship.
    auto verified = verify(*contact0);

    if (verified == API_OK)
        return verify(*contact1);

    return verified;
}

ErrorOr<FileFingerprint> fingerprint(const std::string& content,
                                     std::chrono::system_clock::time_point modified);

ErrorOr<FileFingerprint> fingerprint(const Path& path);

template<typename Container, typename Function>
void forEach(Container&& container, Function function)
{
    std::for_each(std::begin(container), std::end(container), std::move(function));
}

DateTime lastWriteTime(const Path& path, std::error_code& result);
DateTime lastWriteTime(const Path& path);

void lastWriteTime(const Path path, const DateTime& modified, std::error_code& result);

void lastWriteTime(const Path& path, const DateTime& modified);

std::string randomBytes(std::size_t length);

std::string randomName();

template<typename Predicate, typename Result = decltype(std::declval<Predicate>()())>
auto waitUntil(Predicate&& predicate,
               std::chrono::steady_clock::time_point when,
               Result defaultValue = Result()) -> decltype(predicate())
{
    // How long should we wait between tests?
    constexpr auto step = std::chrono::milliseconds(256);

    // Wait until when for predicate to be satisifed.
    while (true)
    {
        // Convenience.
        auto now = std::chrono::steady_clock::now();

        // Predicate is satisfied.
        if (auto result = predicate())
            return result;

        // Predicate's taken too long to be satisfied.
        if (now >= when)
            return defaultValue;

        // When should we test the predicate again?
        auto next = std::min(now + step, when);

        // Sleep until then.
        std::this_thread::sleep_until(next);
    }
}

template<typename Predicate,
         typename Period,
         typename Rep,
         typename Result = decltype(std::declval<Predicate>()())>
auto waitFor(Predicate&& predicate,
             std::chrono::duration<Rep, Period> timeout,
             Result defaultValue = Result()) -> decltype(predicate())
{
    return waitUntil(std::move(predicate),
                     std::chrono::steady_clock::now() + timeout,
                     defaultValue);
}

} // testing
} // fuse
} // mega
