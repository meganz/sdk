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

    static std::string getOqsDataComments(const int idx)
    {
        switch (idx)
        {
            case 1:
                return "Very bad";
            case 2:
                return "Bad";
            case 3:
                return "Normal";
            case 4:
                return "Good";
            case 5:
                return "Very good";
            default:
                return "Invalid value";
        }
    }

    /**
     * @brief Generates a survey response and comment based on the trigger action ID and rating.
     *
     * This function generates a pair of strings: a response and a comment. The response is the
     * string representation of the provided numeric rating value, and the comment is obtained from
     * the `getOqsDataComments` function based on the rating.
     *
     * @param triggerActionId The ID representing the action that triggers the survey generation.
     * @param rating The rating provided by the user, which is converted to a string for the
     * response.
     *
     * @return A `std::pair<std::string, std::string>` containing:
     *         - The response string representing the rating.
     *         - The comment string generated based on the rating.
     *
     * @note If the `triggerActionId` is not equal to `MegaApi::ACT_END_UPLOAD`, the function will
     * return an empty response and comment pair.
     */
    std::pair<std::string, std::string> generateUploadSurveyInfo(const unsigned int triggerActionId,
                                                                 const int rating = -1);

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

    // A test survey configured for end upload trigger event
    mTextSurvey.triggerActionId = MegaApi::ACT_END_UPLOAD;
    mTextSurvey.h = toHandle("zqdkqTtOtGc");

    // A test survey configured for end meeting trigger event
    mIntegerSurvey.triggerActionId = MegaApi::ACT_END_MEETING;
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

std::pair<std::string, std::string>
    OneQuestionSurveyTest::generateUploadSurveyInfo(const unsigned int triggerActionId,
                                                    const int rating)
{
    if (triggerActionId == MegaApi::ACT_END_UPLOAD)
    {
        const std::string response = numberToString<int>(rating);
        const std::string comment = getOqsDataComments(rating);
        return {response, comment};
    }

    return {};
}

TEST_F(OneQuestionSurveyTest, RetrieveSurveyWithNonExistentActionIdShouldFail)
{
    LOG_info << "___TEST OneQuestionSurveyTest::RetrieveSurveyWithNonExistentActionIdShouldFail";

    // Attempting to retrieve a survey with a non-existent trigger action ID should fail.
    ASSERT_EQ(getSurvey(99999u)->waitForResult(), API_ENOENT);
}

/**
 * @brief TEST_F OneQuestionSurveyTest.AnswerUploadResponseSurveyShouldSucceed
 *
 * Tests OneQuestionSurvey for type ACT_END_UPLOAD
 *
 * # Test1: U1 - Retrieving the text response survey's trigger action ID should be successful.
 * # Test2: U1 - Retrieving the text response survey (with 0 maxResponse) should be successful.
 * # Test3: U1 - Answers survey with wrong parameters (Wrong response param)
 * # Test4: U1 - Answers survey with wrong parameters (Wrong response param)
 * # Test5: U1 - Answers survey with wrong parameters (Wrong rating value)
 * # Test6: U1 - Answers survey successfully
 * # Test7: U1 - Answers survey successfully (with empty comment param)
 */
TEST_F(OneQuestionSurveyTest, AnswerUploadResponseSurveyShouldSucceed)
{
    LOG_info << "___TEST OneQuestionSurveyTest::AnswerResponseSurveyShouldSucceed";

    // Enable testing for pre-configured text response survey should be successfully
    ASSERT_EQ(enableTestSurveys({mTextSurvey.h})->waitForResult(), API_OK);

    LOG_debug << "#### Test1(AnswerUploadResponseSurveyShouldSucceed): U1 - Retrieving the text "
                 "response survey's trigger action ID should be successful. ####";
    auto triggersTracker = getActiveSurveyTriggerActions();
    ASSERT_EQ(triggersTracker->waitForResult(), API_OK);
    const auto triggers = toIntegerSet(triggersTracker->request->getMegaIntegerList());
    ASSERT_TRUE(triggers.count(mTextSurvey.triggerActionId));

    LOG_debug
        << "#### Test2(AnswerUploadResponseSurveyShouldSucceed): Retrieving the text response "
           "survey (with 0 maxResponse) should be successful. ####";
    Survey textSurvey;
    ASSERT_NO_FATAL_FAILURE(getOneActiveSurvey(mTextSurvey.triggerActionId, textSurvey));
    ASSERT_EQ(textSurvey.h, mTextSurvey.h);
    ASSERT_EQ(textSurvey.maxResponse, 0);

    LOG_debug << "#### Test3(AnswerUploadResponseSurveyShouldSucceed): U1 - Answers survey with "
                 "wrong parameters (Wrong response param). ####";
    ASSERT_EQ(
        answerSurvey(textSurvey.h, textSurvey.triggerActionId, "Awesome", "")->waitForResult(),
        API_EARGS);

    LOG_debug << "#### Test4(AnswerUploadResponseSurveyShouldSucceed): U1 - Answers survey with "
                 "wrong parameters (Wrong response param). ####";
    ASSERT_EQ(answerSurvey(textSurvey.h, textSurvey.triggerActionId, "6 Star!", "Awesome")
                  ->waitForResult(),
              API_EARGS);

    LOG_debug << "#### Test5(AnswerUploadResponseSurveyShouldSucceed): U1 - Answers survey with "
                 "wrong parameters (Wrong rating value). ####";
    auto [r1, c1] = generateUploadSurveyInfo(textSurvey.triggerActionId, 7);
    ASSERT_EQ(answerSurvey(textSurvey.h, textSurvey.triggerActionId, r1.c_str(), c1.c_str())
                  ->waitForResult(),
              API_EARGS);

    LOG_debug << "#### Test6(AnswerUploadResponseSurveyShouldSucceed): Test6: U1 - Answers survey "
                 "successfully. ####";
    auto [r2, c2] = generateUploadSurveyInfo(textSurvey.triggerActionId, 5);
    ASSERT_EQ(answerSurvey(textSurvey.h, textSurvey.triggerActionId, r2.c_str(), c2.c_str())
                  ->waitForResult(),
              API_OK);

    LOG_debug << "#### Test7(AnswerUploadResponseSurveyShouldSucceed): U1 - Test7: U1 - Answers "
                 "survey successfully (with empty comment param). ####";
    auto [r3, _] = generateUploadSurveyInfo(textSurvey.triggerActionId, 3);
    ASSERT_EQ(
        answerSurvey(textSurvey.h, textSurvey.triggerActionId, r3.c_str(), "")->waitForResult(),
        API_OK);

    // Clearing testing surveys should be successful
    ASSERT_EQ(enableTestSurveys({})->waitForResult(), API_OK);
}

TEST_F(OneQuestionSurveyTest, AnswerEndCallResponseSurveyShouldSucceed)
{
    LOG_info << "___TEST OneQuestionSurveyTest::AnswerEndCallResponseSurveyShouldSucceed";

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

    // Different answers
    ASSERT_EQ(
        answerSurvey(integerSurvey.h, integerSurvey.triggerActionId, "1", "")->waitForResult(),
        API_OK);

    ASSERT_EQ(answerSurvey(mIntegerSurvey.h,
                           mIntegerSurvey.triggerActionId,
                           std::to_string(integerSurvey.maxResponse),
                           "Awesome")
                  ->waitForResult(),
              API_OK);

    // Clearing testing surveys should be successful
    ASSERT_EQ(enableTestSurveys({})->waitForResult(), API_OK);
}

TEST_F(OneQuestionSurveyTest, AnswerTextSurveyWronglyShouldFail)
{
    LOG_info << "___TEST OneQuestionSurveyTest::AnswerTextSurveyWronglyShouldFail";

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

    // Answer using the wrong trigger action ID
    const unsigned int wrongTriggerActionId = textSurvey.triggerActionId + 1;
    ASSERT_EQ(answerSurvey(textSurvey.h, wrongTriggerActionId, "awesome", "")->waitForResult(),
              API_ENOENT);

    // Answer using the wrong handle
    const handle wrongHandle = textSurvey.h + 1;
    ASSERT_EQ(answerSurvey(wrongHandle, textSurvey.triggerActionId, "awesome", "")->waitForResult(),
              API_EARGS);

    // Answer using empty response
    ASSERT_EQ(answerSurvey(textSurvey.h, textSurvey.triggerActionId, "", "")->waitForResult(),
              API_EARGS);

    // Clearing testing surveys should be successful
    ASSERT_EQ(enableTestSurveys({})->waitForResult(), API_OK);
}

TEST_F(OneQuestionSurveyTest, AnswerIntegerSurveyWronglyShouldFail)
{
    LOG_info << "___TEST OneQuestionSurveyTest::AnswerIntegerSurveyWronglyShouldFail";

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
    const unsigned int wrongTriggerActionId = integerSurvey.triggerActionId + 1;
    ASSERT_EQ(answerSurvey(integerSurvey.h, wrongTriggerActionId, "1", "")->waitForResult(),
              API_ENOENT);

    // Answer using the wrong handle
    const handle wrongHandle = integerSurvey.h + 1;
    ASSERT_EQ(answerSurvey(wrongHandle, integerSurvey.triggerActionId, "1", "")->waitForResult(),
              API_EARGS);

    // Answer using empty response
    ASSERT_EQ(answerSurvey(integerSurvey.h, integerSurvey.triggerActionId, "", "")->waitForResult(),
              API_EARGS);

    // Answer using non integer response
    ASSERT_EQ(
        answerSurvey(integerSurvey.h, integerSurvey.triggerActionId, "nonint", "")->waitForResult(),
        API_EARGS);

    // Answer using a response which is out of (0..maxResponse] range
    ASSERT_EQ(
        answerSurvey(integerSurvey.h, integerSurvey.triggerActionId, "0", "")->waitForResult(),
        API_EARGS);

    ASSERT_EQ(answerSurvey(integerSurvey.h,
                           integerSurvey.triggerActionId,
                           std::to_string(integerSurvey.maxResponse + 1),
                           "")
                  ->waitForResult(),
              API_EARGS);

    // Clearing testing surveys should be successful
    ASSERT_EQ(enableTestSurveys({})->waitForResult(), API_OK);
}
