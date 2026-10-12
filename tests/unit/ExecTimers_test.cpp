/**
 * @file
 * @brief Tests for MegaClient::execTimers, which fires the expired bttimers.
 */

#include "mega/backofftimer.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "utils.h"

#include <gtest/gtest.h>

using namespace mega;

namespace
{
struct TimerResultApp: MegaApp
{
    void timer_result(error e) override
    {
        mResults.emplace_back(client->restag, e);
    }

    std::vector<std::pair<int, error>> mResults;
};

class ExecTimers: public ::testing::Test
{
protected:
    void SetUp() override
    {
        Waiter::ds = 1000;
    }

    void TearDown() override
    {
        Waiter::ds = mSavedDs;
    }

    CallbackTimer* addCallbackTimer(const dstime delay, std::function<void()> callback)
    {
        auto* timer = new CallbackTimer(client->rng, std::move(callback));
        timer->backoff(delay);
        client->addtimer(timer);
        return timer;
    }

    const dstime mSavedDs = Waiter::ds;
    TimerResultApp app;
    std::shared_ptr<MegaClient> client = mt::makeClient(app);
};

TEST_F(ExecTimers, FiresOnlyTheExpiredTimers)
{
    int early = 0;
    int late = 0;
    addCallbackTimer(10,
                     [&early]()
                     {
                         ++early;
                     });
    addCallbackTimer(20,
                     [&late]()
                     {
                         ++late;
                     });

    Waiter::ds = 1010;
    client->execTimers();
    EXPECT_EQ(1, early);
    EXPECT_EQ(0, late) << "a timer fired before it expired";

    Waiter::ds = 1020;
    client->execTimers();
    EXPECT_EQ(1, early) << "a fired timer fired again";
    EXPECT_EQ(1, late);
}

TEST_F(ExecTimers, ATimerAddedByACallbackFiresOnTheNextPass)
{
    int readded = 0;
    addCallbackTimer(0,
                     [this, &readded]()
                     {
                         // Already armed when added: must still wait for the next pass
                         addCallbackTimer(0,
                                          [&readded]()
                                          {
                                              ++readded;
                                          });
                     });

    client->execTimers();
    EXPECT_EQ(0, readded) << "a timer added during execTimers fired in the same pass";

    client->execTimers();
    EXPECT_EQ(1, readded);

    client->execTimers();
    EXPECT_EQ(1, readded) << "a fired timer stayed queued";
}

TEST_F(ExecTimers, AnAppTimerReportsTimerResultWithItsTag)
{
    auto* timer = new TimerWithBackoff(client->rng, 42);
    timer->backoff(10);
    client->addtimer(timer);

    Waiter::ds = 1010;
    client->execTimers();
    EXPECT_EQ((std::vector<std::pair<int, error>>{{42, API_OK}}), app.mResults);

    client->execTimers();
    EXPECT_EQ(1u, app.mResults.size()) << "a fired timer stayed queued";
}
} // namespace
