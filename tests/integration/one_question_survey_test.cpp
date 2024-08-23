#include "SdkTest_test.h"

#include <gtest/gtest.h>

class SdkTestOneQuestionSurvey: public SdkTest
{
protected:
    struct Survey
    {
        unsigned int triggerActionId;

        // Survey handle
        handle h{UNDEF};

        // Maximum allowed value in the survey response.
        unsigned int maxResponse{0};

        // Name of an image to be display
        std::string image;

        // Content of the question
        std::string content;
    };

    void SetUp() override;

    std::set<unsigned int> toIntegerSet(const MegaIntegerList* list) const;

    handle toHandle(const char* handleInB64) const;

    std::unique_ptr<MegaHandleList> toMegaHandleList(const std::vector<handle>& handles) const;

    void getOneActiveSurvey(unsigned int triggerActionId, Survey& survey) const;

    Survey mTextSurvey;
    Survey mIntegerSurvey;
};

//
// To streamline the test case, two pre-configured test surveys should be
// utilized. These surveys are set up to be returned by the API with priority
// when they are enabled for testing. The details are as follows:
//
// Text Response Test Survey:
//   Trigger Action ID: 1
//   Survey Handle: zqdkqTtOtGc
// Integer Response Test Survey:
//   Trigger Action ID: 2
//   Survey Handle: j-r9sea9qW4
//
// Only the trigger action ID and handle need to be tested; other fields can be ignored.
void SdkTestOneQuestionSurvey::SetUp()
{
    SdkTest::SetUp();

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // A test survey configured for text responses
    mTextSurvey.triggerActionId = 1;
    mTextSurvey.h = toHandle("zqdkqTtOtGc");

    // A test survey configured for integer responses
    mIntegerSurvey.triggerActionId = 2;
    mIntegerSurvey.h = toHandle("j-r9sea9qW4");
}

std::set<unsigned int> SdkTestOneQuestionSurvey::toIntegerSet(const MegaIntegerList* list) const
{
    set<unsigned int> result;
    if (!list || list->size() == 0)
        return result;

    for (int i = 0; i < list->size(); ++i)
    {
        result.emplace(list->get(i));
    }

    return result;
}

std::unique_ptr<MegaHandleList>
    SdkTestOneQuestionSurvey::toMegaHandleList(const std::vector<handle>& handles) const
{
    std::unique_ptr<MegaHandleList> list{MegaHandleList::createInstance()};
    for (const auto& handle: handles)
    {
        list->addMegaHandle(handle);
    }
    return list;
}

handle SdkTestOneQuestionSurvey::toHandle(const char* handleInB64) const
{
    handle surveyHandle{UNDEF};
    Base64::atob(handleInB64,
                 reinterpret_cast<::mega::byte*>(&surveyHandle),
                 MegaClient::SURVEYHANDLE);
    return surveyHandle;
}

void SdkTestOneQuestionSurvey::getOneActiveSurvey(unsigned int triggerActionId,
                                                  SdkTestOneQuestionSurvey::Survey& survey) const
{
    RequestTracker tracker{megaApi[0].get()};

    megaApi[0]->getSurvey(triggerActionId, &tracker);

    ASSERT_EQ(tracker.waitForResult(), API_OK) << "Get a survey should succeed";

    const auto& request = tracker.request;
    survey.triggerActionId = triggerActionId;
    survey.h = request->getNodeHandle();
    survey.maxResponse = static_cast<unsigned int>(request->getNumDetails());
    survey.image = request->getFile() ? std::string{request->getFile()} : "";
    survey.content = request->getText() ? std::string{request->getText()} : "";
}

TEST_F(SdkTestOneQuestionSurvey, Test)
{
    LOG_info << "___TEST SdkTestOneQuestionSurvey::Test";

    // Attempting to retrieve a survey with a non-existent trigger action ID should fail.
    RequestTracker notExistTriggerActionIdtracker{megaApi[0].get()};
    megaApi[0]->getSurvey(99999u, &notExistTriggerActionIdtracker);
    ASSERT_EQ(notExistTriggerActionIdtracker.waitForResult(), API_ENOENT);

    // Enable testing for two pre-configured surveys
    RequestTracker enableTestSurveyTracker{megaApi[0].get()};
    megaApi[0]->enableTestSurveys(
        toMegaHandleList(std::vector<handle>{mTextSurvey.h, mIntegerSurvey.h}).get(),
        &enableTestSurveyTracker);
    ASSERT_EQ(enableTestSurveyTracker.waitForResult(), API_OK)
        << "Enable testing for surveys should succeed.";

    // Retrieving all active trigger actions should be successful.
    RequestTracker allTriggersTracker{megaApi[0].get()};
    megaApi[0]->getActiveSurveyTriggerActions(&allTriggersTracker);
    ASSERT_EQ(allTriggersTracker.waitForResult(), API_OK);
    const auto allTriggers = toIntegerSet(allTriggersTracker.request->getMegaIntegerList());
    ASSERT_GE(allTriggers.size(), 2) << "There must be at least two active trigger actions.";
    ASSERT_TRUE(allTriggers.count(mTextSurvey.triggerActionId))
        << "Unable to locate the trigger action ID for the text response survey.";
    ASSERT_TRUE(allTriggers.count(mIntegerSurvey.triggerActionId))
        << "Unable to locate the trigger action ID of the integer response survey";

    // Retrieving the text response survey should be successful.
    Survey textSurvey;
    ASSERT_NO_FATAL_FAILURE(getOneActiveSurvey(mTextSurvey.triggerActionId, textSurvey));
    ASSERT_EQ(textSurvey.h, mTextSurvey.h) << "The handle for the text survey is incorrect.";
    ASSERT_EQ(textSurvey.maxResponse, 0)
        << "A text response survey should have a maxResponse value of zero.";

    // Retrieving the integer response survey should be successful.
    Survey integerSurvey;
    ASSERT_NO_FATAL_FAILURE(getOneActiveSurvey(mIntegerSurvey.triggerActionId, integerSurvey));
    ASSERT_EQ(integerSurvey.h, mIntegerSurvey.h)
        << "The handle for the integer survey is incorrect.";
    ASSERT_GT(integerSurvey.maxResponse, 0)
        << "An integer response survey should have a positive maxResponse value.";

    // Clearing testing surveys should be successful
    RequestTracker clearTestSurveyTracker{megaApi[0].get()};
    megaApi[0]->enableTestSurveys(toMegaHandleList(std::vector<handle>{}).get(),
                                  &clearTestSurveyTracker);
    ASSERT_EQ(clearTestSurveyTracker.waitForResult(), API_OK)
        << "Clearing testing surveys should succeed.";
}
