#include "SdkTest_test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

class SdkTestOneQuestionSurvey: public SdkTest
{
protected:
    struct Survey
    {
        bool isValid() const
        {
            return h != UNDEF;
        };

        // Survey handle
        handle h{UNDEF};

        // Maximum allowed value in the survey response.
        unsigned int maxResponse{0};

        // Name of an image to be display
        std::string image;

        // Content of the question
        std::string content;
    };

    using SurveyContainer = std::vector<Survey>;

    void SetUp() override;

    void TearDown() override;

    std::unique_ptr<MegaHandleList> toMegaHandleList(const SurveyContainer& surveys) const;

    std::pair<ErrorCodes, std::vector<unsigned int>> getActiveSurveyTriggerActions() const;

    std::pair<ErrorCodes, Survey> getOneActiveSurvey(unsigned int triggerActionId) const;

    std::pair<ErrorCodes, SurveyContainer>
        getAllActiveSurveys(const std::vector<unsigned int>& triggerActions) const;

    ErrorCodes enableTestSurveys(const MegaHandleList* surveyHandles) const;

    ErrorCodes clearTestSurveys() const;

    std::pair<ErrorCodes, SurveyContainer> getAllActiveSurveys() const;

    void testGetNotExistSurveyReturnsError();

    void testAnswerSurveysSuccessfully();
};

std::unique_ptr<MegaHandleList>
    SdkTestOneQuestionSurvey::toMegaHandleList(const SurveyContainer& surveys) const
{
    std::unique_ptr<MegaHandleList> handles{MegaHandleList::createInstance()};
    for (const auto& survey: surveys)
    {
        handles->addMegaHandle(survey.h);
    }
    return handles;
}

std::pair<ErrorCodes, std::vector<unsigned int>>
    SdkTestOneQuestionSurvey::getActiveSurveyTriggerActions() const
{
    std::vector<unsigned int> triggerActions;

    RequestTracker tracker{megaApi[0].get()};
    megaApi[0]->getActiveSurveyTriggerActions(&tracker);
    if (auto e = tracker.waitForResult(); e != API_OK)
    {
        LOG_err << "getActiveSurveyTriggerActions error: " << e;
        return {e, std::move(triggerActions)};
    }

    const auto* l = tracker.request->getMegaIntegerList();

    // Add trigger actions to container
    for (int i = 0; i < l->size(); ++i)
    {
        triggerActions.emplace_back(static_cast<unsigned int>(l->get(i)));
    }

    return {API_OK, std::move(triggerActions)};
}

std::pair<ErrorCodes, SdkTestOneQuestionSurvey::Survey>
    SdkTestOneQuestionSurvey::getOneActiveSurvey(unsigned int triggerActionId) const
{
    Survey survey;

    RequestTracker tracker{megaApi[0].get()};

    megaApi[0]->getSurvey(triggerActionId, &tracker);

    if (auto e = tracker.waitForResult(); e != API_OK)
    {
        LOG_err << "getSurvey " << triggerActionId << "error: " << e;
        return {e, std::move(survey)};
    }

    const auto* request = tracker.request.get();
    survey.h = request->getNodeHandle();
    survey.maxResponse = static_cast<unsigned int>(request->getNumDetails());
    survey.image = request->getFile() ? std::string{request->getFile()} : "";
    survey.content = request->getText() ? std::string{request->getText()} : "";

    return {API_OK, std::move(survey)};
}

std::pair<ErrorCodes, SdkTestOneQuestionSurvey::SurveyContainer>
    SdkTestOneQuestionSurvey::getAllActiveSurveys(
        const std::vector<unsigned int>& triggerActions) const
{
    SurveyContainer surveys;
    ErrorCodes error = API_OK;

    for (const auto& triggerAction: triggerActions)
    {
        if (const auto [e, survey] = getOneActiveSurvey(triggerAction); e != API_OK)
        {
            error = e;
            break;
        }
        else
        {
            surveys.push_back(std::move(survey));
        }
    }

    return {error, std::move(surveys)};
}

std::pair<ErrorCodes, SdkTestOneQuestionSurvey::SurveyContainer>
    SdkTestOneQuestionSurvey::getAllActiveSurveys() const
{
    if (const auto [e, triggerActions] = getActiveSurveyTriggerActions(); e != API_OK)
    {
        return {e, SurveyContainer{}};
    }
    else
    {
        return getAllActiveSurveys(triggerActions);
    }
}

ErrorCodes SdkTestOneQuestionSurvey::clearTestSurveys() const
{
    return enableTestSurveys(toMegaHandleList(SurveyContainer{}).get());
}

ErrorCodes SdkTestOneQuestionSurvey::enableTestSurveys(const MegaHandleList* surveyHandles) const
{
    RequestTracker tracker{megaApi[0].get()};
    megaApi[0]->enableTestSurveys(surveyHandles, &tracker);

    auto e = tracker.waitForResult();
    if (e != API_OK)
    {
        LOG_err << "enableTestSurveys error: " << e;
    }

    return e;
}

void SdkTestOneQuestionSurvey::testGetNotExistSurveyReturnsError()
{
    LOG_info << "testGetNotExistSurveyReturnsError";

    unsigned int notExistTriggerActionId = 99999;
    auto [e, survey] = getOneActiveSurvey(notExistTriggerActionId);
    ASSERT_EQ(e, API_ENOENT);
}

//
// At least two pre-configured test surveys are available:
// one configured for text responses and another for integer responses.
//
void SdkTestOneQuestionSurvey::testAnswerSurveysSuccessfully()
{
    LOG_info << "testAnswerSurveysSuccessfully";

    auto [e, surveys] = getAllActiveSurveys();
    ASSERT_EQ(e, API_OK);
    ASSERT_GE(surveys.size(), 2); // at least two surveys
    ASSERT_TRUE(all_of(std::begin(surveys),
                       std::end(surveys),
                       [](const Survey& survey)
                       {
                           return survey.isValid();
                       }));

    ASSERT_EQ(clearTestSurveys(), API_OK);

    auto handles = toMegaHandleList(surveys);
    ASSERT_EQ(enableTestSurveys(handles.get()), API_OK);

    // TODO answer survey

    ASSERT_EQ(clearTestSurveys(), API_OK);
}

void SdkTestOneQuestionSurvey::SetUp()
{
    SdkTest::SetUp();

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
}

void SdkTestOneQuestionSurvey::TearDown()
{
    SdkTest::TearDown();
}

TEST_F(SdkTestOneQuestionSurvey, Test)
{
    LOG_info << "___TEST SdkTestOneQuestionSurvey::Test";

    ASSERT_NO_FATAL_FAILURE(testGetNotExistSurveyReturnsError());

    ASSERT_NO_FATAL_FAILURE(testAnswerSurveysSuccessfully());
}
