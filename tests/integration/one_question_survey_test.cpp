#include "SdkTest_test.h"

#include <gtest/gtest.h>

class OneQuestionSurveyTest: public SdkTest
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

    std::unique_ptr<RequestTracker> enableTestSurveys(const std::vector<handle>& handles) const;

    std::unique_ptr<RequestTracker> getSurvey(unsigned int triggerActionId) const;

    std::unique_ptr<RequestTracker> getActiveSurveyTriggerActions() const;

    std::unique_ptr<RequestTracker> answerSurvey(MegaHandle surveyHandle,
                                                 unsigned int triggerActionId,
                                                 const std::string& response,
                                                 const std::string& comment) const;

    Survey mTextSurvey;

    Survey mIntegerSurvey;
};

//
// To streamline the test case, two pre-configured test surveys should be
// utilized. These surveys are set up to be returned by the API with priority
// when they are enabled for testing. The details are as follows:
//
// Text Response Test Survey (a survey with 0 maxResponse):
//   Trigger Action ID: 1
//   Survey Handle: zqdkqTtOtGc
// Integer Response Test Survey (a survey with positive maxResponse):
//   Trigger Action ID: 2
//   Survey Handle: j-r9sea9qW4
//
// Only the trigger action ID and handle need to be tested; other fields can be ignored.
void OneQuestionSurveyTest::SetUp()
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

std::set<unsigned int> OneQuestionSurveyTest::toIntegerSet(const MegaIntegerList* list) const
{
    set<unsigned int> result;
    if (!list || list->size() == 0)
        return result;

    for (int i = 0; i < list->size(); ++i)
    {
        result.emplace(static_cast<unsigned int>(list->get(i)));
    }

    return result;
}

std::unique_ptr<MegaHandleList>
    OneQuestionSurveyTest::toMegaHandleList(const std::vector<handle>& handles) const
{
    std::unique_ptr<MegaHandleList> list{MegaHandleList::createInstance()};
    for (const auto& handle: handles)
    {
        list->addMegaHandle(handle);
    }
    return list;
}

handle OneQuestionSurveyTest::toHandle(const char* handleInB64) const
{
    handle surveyHandle{UNDEF};
    Base64::atob(handleInB64,
                 reinterpret_cast<::mega::byte*>(&surveyHandle),
                 MegaClient::SURVEYHANDLE);
    return surveyHandle;
}

void OneQuestionSurveyTest::getOneActiveSurvey(unsigned int triggerActionId,
                                               OneQuestionSurveyTest::Survey& survey) const
{
    auto tracker = getSurvey(triggerActionId);

    ASSERT_EQ(tracker->waitForResult(), API_OK);

    const auto& request = tracker->request;
    survey.triggerActionId = triggerActionId;
    survey.h = request->getNodeHandle();
    survey.maxResponse = static_cast<unsigned int>(request->getNumDetails());
    survey.image = request->getFile() ? std::string{request->getFile()} : "";
    survey.content = request->getText() ? std::string{request->getText()} : "";
}

std::unique_ptr<RequestTracker>
    OneQuestionSurveyTest::enableTestSurveys(const std::vector<handle>& handles) const
{
    return RequestTracker::async(*megaApi[0],
                                 &MegaApi::enableTestSurveys,
                                 toMegaHandleList(handles).get());
}

std::unique_ptr<RequestTracker> OneQuestionSurveyTest::getSurvey(unsigned int triggerActionId) const
{
    return RequestTracker::async(*megaApi[0], &MegaApi::getSurvey, triggerActionId);
}

std::unique_ptr<RequestTracker> OneQuestionSurveyTest::getActiveSurveyTriggerActions() const
{
    return RequestTracker::async(*megaApi[0], &MegaApi::getActiveSurveyTriggerActions);
}

std::unique_ptr<RequestTracker>
    OneQuestionSurveyTest::answerSurvey(MegaHandle surveyHandle,
                                        unsigned int triggerActionId,
                                        const std::string& response,
                                        const std::string& comment) const
{
    return RequestTracker::async(*megaApi[0],
                                 &MegaApi::answerSurvey,
                                 surveyHandle,
                                 triggerActionId,
                                 response.c_str(),
                                 comment.c_str());
}

TEST_F(OneQuestionSurveyTest, RetrieveSurveyWithNonExistentActionIdShouldFail)
{
    LOG_info << "___TEST OneQuestionSurveyTest::RetrieveSurveyWithNonExistentActionIdShouldFail";

    // Attempting to retrieve a survey with a non-existent trigger action ID should fail.
    ASSERT_EQ(getSurvey(99999u)->waitForResult(), API_ENOENT);
}

TEST_F(OneQuestionSurveyTest, RetrieveTextResponseSurveyShouldSucceed)
{
    LOG_info << "___TEST OneQuestionSurveyTest::RetrieveTextResponseSurveyShouldSucceed";

    // Enable testing for pre-configured text response survey should be successfully
    ASSERT_EQ(enableTestSurveys({mTextSurvey.h})->waitForResult(), API_OK);

    // Retrieving the text response survey's trigger action ID should be successful.
    auto triggersTracker = getActiveSurveyTriggerActions();
    ASSERT_EQ(triggersTracker->waitForResult(), API_OK);
    const auto triggers = toIntegerSet(triggersTracker->request->getMegaIntegerList());
    ASSERT_TRUE(triggers.count(mTextSurvey.triggerActionId));

    // Retrieving the text response survey (with 0 maxResponse) should be successful.
    Survey textSurvey;
    ASSERT_NO_FATAL_FAILURE(getOneActiveSurvey(mTextSurvey.triggerActionId, textSurvey));
    ASSERT_EQ(textSurvey.h, mTextSurvey.h);
    ASSERT_EQ(textSurvey.maxResponse, 0);

    // Clearing testing surveys should be successful
    ASSERT_EQ(enableTestSurveys({})->waitForResult(), API_OK);
}

TEST_F(OneQuestionSurveyTest, RetrieveIntegerResponseSurveyShouldSucceed)
{
    LOG_info << "___TEST OneQuestionSurveyTest::RetrieveIntegerResponseSurveyShouldSucceed";

    // Enable testing for pre-configured integer response survey should be successfully
    ASSERT_EQ(enableTestSurveys({mIntegerSurvey.h})->waitForResult(), API_OK);

    // Retrieving the integer response survey's trigger action ID should be successful.
    auto triggersTracker = getActiveSurveyTriggerActions();
    ASSERT_EQ(triggersTracker->waitForResult(), API_OK);
    const auto triggers = toIntegerSet(triggersTracker->request->getMegaIntegerList());
    ASSERT_TRUE(triggers.count(mIntegerSurvey.triggerActionId));

    // Retrieving the integer response survey (with positive maxResponse) should be successful.
    Survey integerSurvey;
    ASSERT_NO_FATAL_FAILURE(getOneActiveSurvey(mIntegerSurvey.triggerActionId, integerSurvey));
    ASSERT_EQ(integerSurvey.h, mIntegerSurvey.h);
    ASSERT_GT(integerSurvey.maxResponse, 0);

    // Clearing testing surveys should be successful
    ASSERT_EQ(enableTestSurveys({})->waitForResult(), API_OK);
}

TEST_F(OneQuestionSurveyTest, AnswerSurveyUsingWrongTriggerActionIdOrHandleShouldFail)
{
    LOG_info
        << "___TEST OneQuestionSurveyTest::AnswerSurveyUsingWrongTriggerActionIdOrHandleShouldFail";

    // Enable testing for pre-configured integer response survey should be successfully
    ASSERT_EQ(enableTestSurveys({mIntegerSurvey.h})->waitForResult(), API_OK);

    // Retrieving the integer response survey's trigger action ID should be successful.
    auto triggersTracker = getActiveSurveyTriggerActions();
    ASSERT_EQ(triggersTracker->waitForResult(), API_OK);
    const auto triggers = toIntegerSet(triggersTracker->request->getMegaIntegerList());
    ASSERT_TRUE(triggers.count(mIntegerSurvey.triggerActionId));

    // Retrieving the integer response survey (with positive maxResponse) should be successful.
    Survey integerSurvey;
    ASSERT_NO_FATAL_FAILURE(getOneActiveSurvey(mIntegerSurvey.triggerActionId, integerSurvey));
    ASSERT_EQ(integerSurvey.h, mIntegerSurvey.h);
    ASSERT_GT(integerSurvey.maxResponse, 0);

    // Answer using the wrong trigger action ID
    unsigned int wrongTriggerActionId = mIntegerSurvey.triggerActionId + 1;
    auto answerTracker = answerSurvey(mIntegerSurvey.h, wrongTriggerActionId, "1", "");
    ASSERT_EQ(answerTracker->waitForResult(), API_ENOENT);

    // Answer using the wrong handle
    handle wrongHandle = mIntegerSurvey.h + 1;
    answerTracker = answerSurvey(wrongHandle, mIntegerSurvey.triggerActionId, "1", "");
    ASSERT_EQ(answerTracker->waitForResult(), API_EARGS);

    // Clearing testing surveys should be successful
    ASSERT_EQ(enableTestSurveys({})->waitForResult(), API_OK);
}
