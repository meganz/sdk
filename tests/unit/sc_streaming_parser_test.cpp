/**
 * (c) 2025 by Mega Limited, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega/base64.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/sc_streaming_parser.h"
#include "mega/user_attribute.h"
#include "utils.h"

#include <gtest/gtest.h>

using namespace mega;

namespace
{

class ScStreamingParserTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        app = std::make_shared<MegaApp>();
        client = mt::makeClient(*app);
        scStreamingParser = std::make_shared<ScStreamingParser>(*client, client->rng);

        client->statecurrent = true;
    }

    void TearDown() override
    {
        scStreamingParser.reset();
        client.reset();
        app.reset();
    }

    std::shared_ptr<Node> initNodes()
    {
        client->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        client->opensctable();

        // Create root node
        const std::string rootNodeHandleString = "Ll5VkSJZ";
        byte buf[8] = {0};
        Base64::atob(rootNodeHandleString.c_str(), buf, MegaClient::NODEHANDLE);
        const handle rootNodeHandle = MemAccess::get<handle>((const char*)buf);
        std::shared_ptr<Node> rootNode = addNode(ROOTNODE, rootNodeHandle);

        // Create vault node and rubbish node
        addNode(VAULTNODE, 1);
        addNode(RUBBISHNODE, 2);

        return rootNode;
    }

    std::shared_ptr<Node> addNode(nodetype_t nodeType, handle nodeHandle)
    {
        auto node =
            mt::makeNode(*client, nodeType, mega::NodeHandle().set6byte(nodeHandle), nullptr);

        NodeManager::MissingParentNodes missingParentNodes;

        client->mNodeManager.addNode(node, false, false, missingParentNodes);

        return node;
    }

    // Feed one "ua" action packet through the parser. init() must be called once by the caller
    // before the first packet: it appends to the parser's filter chain and clear() does not reset
    // it, so re-initializing per packet would register the filters twice.
    void processUserAttrPacket(const char* uhB64,
                               const char* attrName,
                               const char* version,
                               const char* sn)
    {
        const std::string json =
            std::string(R"({"a":[{"a":"ua","u":")") + uhB64 + R"(","ua":[")" + attrName +
            R"("],"v":[")" + version +
            R"("]}],"w":"https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA","sn":")" + sn +
            R"("})";
        ASSERT_GT(scStreamingParser->process(json.c_str()), 0);
        ASSERT_TRUE(scStreamingParser->isFinished());
        ASSERT_FALSE(scStreamingParser->isFailed());
        ASSERT_FALSE(scStreamingParser->isPaused());
        scStreamingParser->clear();
    }

    std::shared_ptr<MegaApp> app;
    std::shared_ptr<MegaClient> client;
    std::shared_ptr<ScStreamingParser> scStreamingParser;
};

bool isNodeTreeLocked(MegaClient& client)
{
    recursive_mutex& m = client.nodeTreeMutex;
    bool held = true;

    std::thread t(
        [&]
        {
            if (m.try_lock())
            {
                held = false;
                m.unlock();
            }
        });

    t.join();

    return held;
}

void testFinishedProcess(std::shared_ptr<ScStreamingParser>& scStreamingParser,
                         MegaClient& client,
                         std::string& json,
                         std::string&& url,
                         const char* sn)
{
    ASSERT_TRUE(scStreamingParser->process(json.c_str()) > 0);

    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_TRUE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());

    ASSERT_TRUE(client.scnotifyurl == url);

    handle snHdl;
    Base64::atob(sn, (byte*)&snHdl, sizeof(snHdl));
    ASSERT_TRUE(client.scsn.getHandle() == snHdl);

    ASSERT_EQ(client.actionpacketsCurrent, true);
    ASSERT_FALSE(isNodeTreeLocked(client));
}

TEST_F(ScStreamingParserTest, InitAndClear)
{
    // Init
    scStreamingParser->init();

    ASSERT_FALSE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());

    // Clear
    scStreamingParser->clear();

    ASSERT_FALSE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
}

TEST_F(ScStreamingParserTest, SetLastReceived)
{
    ASSERT_FALSE(scStreamingParser->isLastReceived());

    scStreamingParser->setLastReceived();

    ASSERT_TRUE(scStreamingParser->isLastReceived());

    scStreamingParser->clear();

    ASSERT_FALSE(scStreamingParser->isLastReceived());
}

TEST_F(ScStreamingParserTest, Process)
{
    std::string json =
        R"({"a":[{"a":"ua","st":"!?>?e)G","u":"k-N5-o6LLkE","ua":["^!stbmp"],"v":["Gt-009Sr-XA"]}],"w":"https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA","sn":"Gt-009Sr-XA"})";

    scStreamingParser->init();

    // Process 1st SC packet
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA",
                        "Gt-009Sr-XA");

    // Clear for next process
    scStreamingParser->clear();

    ASSERT_FALSE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());

    // Process 2nd SC packet
    json =
        R"({"a":[{"a":"ipc","st":"!?>VwgM","p":"gjqrEkfohxc","m":"sdk-jenkins+005-19@mega.nz","msg":"","ps":0,"ts":1761821624,"uts":1761821624}],"w":"https://g.api.mega.co.nz/wsc/wZ9inPVUsAufT1YgyZ_AIA","sn":"P-LLOv0J3u0"})";

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/wZ9inPVUsAufT1YgyZ_AIA",
                        "P-LLOv0J3u0");
}

TEST_F(ScStreamingParserTest, ProcessWithActionNameNotPresent)
{
    std::string json =
        R"({"a":[{"st":"!?>VwgM","u":"k-N5-o6LLkE","ua":["^!stbmp"],"v":["Gt-009Sr-XA"]}],"w":"https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA","sn":"Gt-009Sr-XA"})";

    scStreamingParser->init();

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA",
                        "Gt-009Sr-XA");
}

TEST_F(ScStreamingParserTest, ProcessWithSessionIdPresent)
{
    std::string json1 = R"({"a":[{"a":"mcc","i":")";
    std::string json2 =
        R"(","id":"FpldU29F-WY","u":[{"u":"tUd0zWN1Qn4","p":3},{"u":"ecgkxzbjRsU","p":2}],"cs":0,"ts":1761821986,"g":1,"ou":"tUd0zWN1Qn4"}],"w":"https://g.api.mega.co.nz/wsc/EiDoieE-5Tuklb0sF-bRcw","sn":"xyp4UX7xFZ4"})";

    scStreamingParser->init();

    // Non self-originated
    std::string sessionId = "cpbtkgwuai";
    std::string json = json1 + sessionId + json2;
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/EiDoieE-5Tuklb0sF-bRcw",
                        "xyp4UX7xFZ4");

    scStreamingParser->clear();

    // Self-originated
    sessionId = string(client->sessionid, sizeof(client->sessionid));
    json = json1 + sessionId + json2;
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/EiDoieE-5Tuklb0sF-bRcw",
                        "xyp4UX7xFZ4");
}

TEST_F(ScStreamingParserTest, ProcessWithSeqTagNotPresent)
{
    std::string json =
        R"({"a":[{"a":"ua", "u":"k-N5-o6LLkE","ua":["^!stbmp"],"v":["Gt-009Sr-XA"]}],"w":"https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA","sn":"Gt-009Sr-XA"})";

    scStreamingParser->init();

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA",
                        "Gt-009Sr-XA");
}

TEST_F(ScStreamingParserTest, ProcessChunks)
{
    std::string json =
        R"({"a":[{"a":"ua","isn":"xyp4UX7xFZ4","st":"!?>AmY:","u":"MOte1pKQgDI","ua":["^!prd"],"v":["Dvh72Rs7JBM"]},{"a":"ua","st":"!?>VwgM","u":"k-N5-o6LLkE","ua":["^!stbmp"],"v":["Gt-009Sr-XA"]}],"w":"https://g.api.)";

    scStreamingParser->init();

    // Process the incomplete packet
    size_t consumed = (size_t)scStreamingParser->process(json.c_str());

    ASSERT_TRUE(consumed > 0);
    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
    ASSERT_TRUE(client->scnotifyurl.empty());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_TRUE(isNodeTreeLocked(*client));

    const char* isn = "xyp4UX7xFZ4";
    handle isnHdl;
    Base64::atob(isn, (byte*)&isnHdl, sizeof(isnHdl));
    ASSERT_TRUE(client->scsn.getHandle() == isnHdl);

    // Purge
    json.erase(0, consumed);

    // Process the remained
    std::string jsonRemained =
        json + R"(mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA","sn":"Gt-009Sr-XA"})";

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonRemained,
                        "https://g.api.mega.co.nz/wsc/5lhYq8nqzgIEE8j9OqymmA",
                        "Gt-009Sr-XA");
}

TEST_F(ScStreamingParserTest, ProcessChunksByMove)
{
    initNodes();

    std::string jsonCreate =
        R"({"a":[{"a":"t","st":"!G(<Cq+","t":{"f":[{"h":"i4om2BrJ","t":1,"a":"k8mDAq0-TfskLMyMqcxssQ","k":"vN8A0kvxmC0:cXtlV5RG0QHpBxl-sZWQxA","p":"Ll5VkSJZ","ts":1770364711,"u":"vN8A0kvxmC0","i":0}]},"ou":"vN8A0kvxmC0"}, {"a":"t","st":"!G(<IS$","t":{"f":[{"h":"HtpQjbba","t":1,"a":"STYGCigAjt9fSXwkPSYxVA","k":"vN8A0kvxmC0:3BrxInvwc6gysUP-NmZ8Fg","p":"Ll5VkSJZ","ts":1770364718,"u":"vN8A0kvxmC0","i":0}]},"ou":"vN8A0kvxmC0"}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"JcnE8wc8Xz0"})";
    std::string jsonMovePart1 =
        R"({"a":[{"a":"d","isn":"xyp4UX7xFZ4","i":"fbtekrfnlb","st":"!G(<L_j","n":"i4om2BrJ","m":1,"ou":"vN8A0kvxmC0"},)";
    std::string jsonMovePart2 =
        R"({"a":"t","i":"fbtekrfnlb","t":{"f":[{"h":"i4om2BrJ","p":"HtpQjbba","u":"vN8A0kvxmC0","t":1,"a":"k8mDAq0-TfskLMyMqcxssQ","k":"vN8A0kvxmC0:cXtlV5RG0QHpBxl-sZWQxA","ts":1770364711}],"u":[{"u":"vN8A0kvxmC0","m":"test@mega.co.nz","m2":["test@mega.co.nz"],"pubk":"CADLwMeFsNUFHY5vIHrDF73XMk6lZvRPQlhh67NGgg8WdiZF7vtM4HrCoftYvQEFLM-JF498dEFLNo9f76KydgWSdl_IEFT4KpnuexvJpfwI0eyJRzU1Wt5wegWJw9WPEKxpHGP91VllLPWB31X2FH48angXw9Mf_3nyjNh-q83tMEbT5XEWuL3KqiMdP80XeqJk3TOuCcaSmq5tzj84rzc1sNnsoNkgYPYuGjOqzA8VzURVU6Tp7BV2eJm0x68dCwoJMHiZVNor1fz0I-iB1Vj2Hurr1NmIGLP9AEJOSKE1iwBSmseTOci3KaLcMrwy1rQMx8r9y-KV5eBnt7kPRSCdACAAAAEB","+puCu255":"dLFE7BT0zCf3Q2f0EXVGRUZRtVuY0J_TPZVMkhAI3Hs","+puEd255":"owpFu55NPN4Y93GX8-iDVcbmktEvd-LcYtgMJUILLxU","+sigCu255":"AAAAAGj5nvcOQZQogXSCNe7RkRLP21nkWcr3D3zx2zY0xC0BqY5_Ny_O1JMaN95YtbkO8dgcYFGiHaW1CWnu9njDcGtnKm8A","+sigPubk":"AAAAAGj5nvgwEi2YXMbNhHdsV1IDYs62RzF1p60M4PkVxXx68HGqF7nBQSg69KUbh_yydf36nneGFMm-7UnKr_yZyFei2wsD"}]},"ou":"vN8A0kvxmC0"}],)";
    std::string jsonMovePart3 =
        R"("w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"YydQMr-woLo"})";

    scStreamingParser->init();

    // Create 2 folder
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonCreate,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "JcnE8wc8Xz0");

    // Clear for next process
    scStreamingParser->clear();

    // Process "d" part
    std::string jsonMove = jsonMovePart1;

    size_t consumed = (size_t)scStreamingParser->process(jsonMove.c_str());

    ASSERT_TRUE(consumed > 0);
    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_TRUE(isNodeTreeLocked(*client));

    // isn is received, but is not stored yet.
    const char* sn = "JcnE8wc8Xz0";
    handle snHdl;
    Base64::atob(sn, (byte*)&snHdl, sizeof(snHdl));
    ASSERT_TRUE(client->scsn.getHandle() == snHdl);

    // Purge
    jsonMove.erase(0, consumed);

    // Process "t" part
    jsonMove += jsonMovePart2;

    consumed = (size_t)scStreamingParser->process(jsonMove.c_str());

    ASSERT_TRUE(consumed > 0);
    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_TRUE(isNodeTreeLocked(*client));

    const char* isn = "xyp4UX7xFZ4";
    handle isnHdl;
    Base64::atob(isn, (byte*)&isnHdl, sizeof(isnHdl));
    ASSERT_TRUE(client->scsn.getHandle() == isnHdl);

    // Purge
    jsonMove.erase(0, consumed);

    // Process the remained
    jsonMove += jsonMovePart3;

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonMove,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "YydQMr-woLo");
}

TEST_F(ScStreamingParserTest, ProcessChunksByNewShare)
{
    initNodes();

    std::string jsonSharePart1 =
        R"({"a":[{"a":"s","isn":"xyp4UX7xFZ4","st":"xyp4UX7xFZ4","n":"IipQWaLR","o":"8kCmVc3EkKo","ok":"AAAAAAAAAAAAAAAAAAAAAA","ha":"AAAAAAAAAAAAAAAAAAAAAA","u":"1TZ8GptXHc0","r":1,"ts":1778566872,"ou":"8kCmVc3EkKo"},)";
    std::string jsonSharePart2 =
        R"({"a":"t","st":"xyp4UX7xFZ4","t":{"f":[{"h":"IipQWaLR","p":"Ll5VkSJZ","u":"8kCmVc3EkKo","t":1,"a":"O7nuh_3533RIH7fYMaUMehJhIjSMnfBrJAXfYqxrvWo","k":"8kCmVc3EkKo:aa2HqukMlSVPEeTZIw50Hg/IipQWaLR:EM1Zh1bucpP6seTAB1F7kQ","ts":1778566850},{"h":"digGyIiB","p":"IipQWaLR","u":"8kCmVc3EkKo","t":0,"a":"KDmtBP6en9b36m0jcZPKbuFNtUiRXR-exPL0PwHfnMQVFciXkioOgvsySGPgD_wxzx-MdWXyh7IdiqOsWZDEt41Z9YVjVxKq7ll2ONNQ5pw","k":"8kCmVc3EkKo:IkzbQl4G3kSgnw3gmvVtLD1jRoR4_6zuGQfm918HjCA/IipQWaLR:EZkKEu6pu7RV9ubjF5BMZhmpqPyoIZG1mkWxxX8ZbtE","s":10000,"ts":1778566870}],"u":[{"u":"8kCmVc3EkKo","m":"jye+test1@mega.co.nz","m2":["jye+test1@mega.co.nz"],"pubk":"CACrs5ljxvqSFMEw-6BekVEZW7t-r_hjUmcpuk-BOhs-kCZNrYqQu_8LGpZRJLf2NjsZaSFDn9rC3gK5C2XoZyHCVHKs_-T8lbcrEDhDSUCk_iJd-vAssJqMfs4KBCIhSt5u2jLpOnA2G7tPwotEIrrvJ0p5WpSzwCxkKQAV2bhb3RhRVmxIRGrjqEGKIJiaeRWnHI2MaGmkk4lVPZRiRrBfkeUiVoH70f_-8xRTt7lv0q-INOliDEdJaupq3koXojRrWqav6jDiP8DxJfH1Qolt1YJL-rJZ-7R5iziQqbRYWvEPpAGaRMkoNy0hDxPTqxtDgZN3AJiXlgO85H7ptCzdACAAAAEB","+puCu255":"bdMHaADkAesHzMRh_XH4LdTD2upOS6HxUuRAyds7FTs","+puEd255":"1GyzwZtLBaZ4Vb4OQcWu__pRnqVhBSKrGTPUbHTqu2w","+sigCu255":"AAAAAGj5niiWUs9A75e2JZZ7XyCFU6NA3HYbVMAjHWP33ZeJYF0rxaRc8NmKCUqvNXNY4JWLJGxkBlV9gG0bfnIXv9__JfYL","+sigPubk":"AAAAAGj5ninCXmk40Hj2eYx0uzu0Fy7LC79_GqsEoPlmICMh3sLrZKf1GHkanoWv6D1gYJ5fJu5FAqejRxFHrupTsrutMIIH"}]},"ou":"8kCmVc3EkKo"}],)";
    std::string jsonSharePart3 =
        R"("w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"YydQMr-woLo"})";

    scStreamingParser->init();

    client->scsn.setScsn(0xFFFFFFFF);

    // Process "s" part
    std::string jsonMove = jsonSharePart1;

    size_t consumed = (size_t)scStreamingParser->process(jsonMove.c_str());

    ASSERT_TRUE(consumed > 0);
    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_TRUE(isNodeTreeLocked(*client));

    // isn is received, but is not stored yet.
    ASSERT_TRUE(client->scsn.getHandle() == 0xFFFFFFFF);

    // Purge
    jsonMove.erase(0, consumed);

    // Process "t" part
    jsonMove += jsonSharePart2;

    consumed = (size_t)scStreamingParser->process(jsonMove.c_str());

    ASSERT_TRUE(consumed > 0);
    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_TRUE(isNodeTreeLocked(*client));

    const char* isn = "xyp4UX7xFZ4";
    handle isnHdl;
    Base64::atob(isn, (byte*)&isnHdl, sizeof(isnHdl));
    ASSERT_TRUE(client->scsn.getHandle() == isnHdl);

    // Purge
    jsonMove.erase(0, consumed);

    // Process the remained
    jsonMove += jsonSharePart3;

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonMove,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "YydQMr-woLo");
}

TEST_F(ScStreamingParserTest, ProcessAndPause)
{
    std::string json =
        R"({"a":[{"a":"ipc","st":"!?>?e)G","p":"gjqrEkfohxc","m":"sdk-jenkins+005-19@mega.nz","msg":"","ps":0,"ts":1761821624,"uts":1761821624}],"w":"https://g.api.mega.co.nz/wsc/wZ9inPVUsAufT1YgyZ_AIA","sn":"P-LLOv0J3u0"})";
    std::string jsonRes =
        R"([["!?>?e)G",{"p":"gjqrEkfohxc","m":"sdk-jenkins+005-20@mega.nz","e":"sdk-jenkins+005-19@mega.nz","msg":"Hi contact. This is a testing message","ts":1761821627,"uts":1761821627,"rts":1761821627}]])";

    scStreamingParser->init();

    // Simulate the case: command is sent, but response has not been received
    client->queueCommand(new CommandSetPendingContact(client.get(), "", (opcactions_t)0));
    bool dummy1;
    string dummy2;
    client->reqs.serverrequest(dummy1, client.get(), dummy2);

    size_t consumed = (size_t)scStreamingParser->process(json.c_str());

    ASSERT_TRUE(consumed > 0);
    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_TRUE(scStreamingParser->isPaused());
    ASSERT_TRUE(client->scnotifyurl.empty());
    ASSERT_FALSE(client->scsn.ready());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_FALSE(isNodeTreeLocked(*client));

    // Purge
    json.erase(0, consumed);

    // Response is received
    client->reqs.serverresponse(std::move(jsonRes), client.get());

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/wZ9inPVUsAufT1YgyZ_AIA",
                        "P-LLOv0J3u0");
}

TEST_F(ScStreamingParserTest, ProcessPacketTreeByMove)
{
    initNodes();

    std::string jsonCreate =
        R"({"a":[{"a":"t","st":"!G(<Cq+","t":{"f":[{"h":"i4om2BrJ","t":1,"a":"k8mDAq0-TfskLMyMqcxssQ","k":"vN8A0kvxmC0:cXtlV5RG0QHpBxl-sZWQxA","p":"Ll5VkSJZ","ts":1770364711,"u":"vN8A0kvxmC0","i":0}]},"ou":"vN8A0kvxmC0"}, {"a":"t","st":"!G(<IS$","t":{"f":[{"h":"HtpQjbba","t":1,"a":"STYGCigAjt9fSXwkPSYxVA","k":"vN8A0kvxmC0:3BrxInvwc6gysUP-NmZ8Fg","p":"Ll5VkSJZ","ts":1770364718,"u":"vN8A0kvxmC0","i":0}]},"ou":"vN8A0kvxmC0"}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"JcnE8wc8Xz0"})";
    std::string jsonMove =
        R"({"a":[{"a":"d","i":"fbtekrfnlb","st":"!G(<L_j","n":"i4om2BrJ","m":1,"ou":"vN8A0kvxmC0"},{"a":"t","i":"fbtekrfnlb","t":{"f":[{"h":"i4om2BrJ","p":"HtpQjbba","u":"vN8A0kvxmC0","t":1,"a":"k8mDAq0-TfskLMyMqcxssQ","k":"vN8A0kvxmC0:cXtlV5RG0QHpBxl-sZWQxA","ts":1770364711}],"u":[{"u":"vN8A0kvxmC0","m":"test@mega.co.nz","m2":["test@mega.co.nz"],"pubk":"CADLwMeFsNUFHY5vIHrDF73XMk6lZvRPQlhh67NGgg8WdiZF7vtM4HrCoftYvQEFLM-JF498dEFLNo9f76KydgWSdl_IEFT4KpnuexvJpfwI0eyJRzU1Wt5wegWJw9WPEKxpHGP91VllLPWB31X2FH48angXw9Mf_3nyjNh-q83tMEbT5XEWuL3KqiMdP80XeqJk3TOuCcaSmq5tzj84rzc1sNnsoNkgYPYuGjOqzA8VzURVU6Tp7BV2eJm0x68dCwoJMHiZVNor1fz0I-iB1Vj2Hurr1NmIGLP9AEJOSKE1iwBSmseTOci3KaLcMrwy1rQMx8r9y-KV5eBnt7kPRSCdACAAAAEB","+puCu255":"dLFE7BT0zCf3Q2f0EXVGRUZRtVuY0J_TPZVMkhAI3Hs","+puEd255":"owpFu55NPN4Y93GX8-iDVcbmktEvd-LcYtgMJUILLxU","+sigCu255":"AAAAAGj5nvcOQZQogXSCNe7RkRLP21nkWcr3D3zx2zY0xC0BqY5_Ny_O1JMaN95YtbkO8dgcYFGiHaW1CWnu9njDcGtnKm8A","+sigPubk":"AAAAAGj5nvgwEi2YXMbNhHdsV1IDYs62RzF1p60M4PkVxXx68HGqF7nBQSg69KUbh_yydf36nneGFMm-7UnKr_yZyFei2wsD"}]},"ou":"vN8A0kvxmC0"}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"YydQMr-woLo"})";

    scStreamingParser->init();

    // Create 2 folder
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonCreate,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "JcnE8wc8Xz0");

    // Clear for next process
    scStreamingParser->clear();

    // Move one folder to another
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonMove,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "YydQMr-woLo");
}

TEST_F(ScStreamingParserTest, ProcessPacketTreeByPutNewVersion)
{
    initNodes();

    std::string jsonCreate =
        R"({"a":[{"a":"t","st":"!G(P`#}","t":{"f":[{"h":"r14EgLRY","t":0,"a":"ZLzvJzDefwAGc8lePmEWhU0fO8ET129XDWyA92FcyDw49VI1I-Zi5oB0Fi7lsCo7T4qjpMqT7EfPpgPNs0P6Fw","k":"vN8A0kvxmC0:ypiDHHhWkrbulm5UUMFy3gODiHsoBIZ37wEbzSSQZGU","p":"Ll5VkSJZ","ts":1770366829,"u":"vN8A0kvxmC0","s":0,"i":0}]},"ou":"vN8A0kvxmC0"}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"lUNj6w_irSk"})";
    std::string jsonCreateVer =
        R"({"a":[{"a":"d","st":"!G(Utm1","n":"r14EgLRY","m":1,"v":1,"ou":"vN8A0kvxmC0"},{"a":"t","st":"!G(Utm1","t":{"f":[{"h":"G0wgRQhS","t":0,"a":"PzbbN2J1r_-j_rAq5raGC4SHahy1FZH11tPQbW6SszI94wBAL0uyWgiDtc4zvXThFHjQAlSvirhem-RYH6piKA","k":"vN8A0kvxmC0:vf0AQehM8yISzlRdHhPqZ-icwiL0RZJZJqFoLI6SM0c","p":"Ll5VkSJZ","ts":1770367362,"u":"vN8A0kvxmC0","s":2,"i":0}],"f2":[{"h":"r14EgLRY","p":"G0wgRQhS","u":"vN8A0kvxmC0","t":0,"a":"ZLzvJzDefwAGc8lePmEWhU0fO8ET129XDWyA92FcyDw49VI1I-Zi5oB0Fi7lsCo7T4qjpMqT7EfPpgPNs0P6Fw","k":"vN8A0kvxmC0:ypiDHHhWkrbulm5UUMFy3gODiHsoBIZ37wEbzSSQZGU","s":0,"ts":1770366829}]},"ou":"vN8A0kvxmC0"}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"a7JBKiUvv5Y"})";

    scStreamingParser->init();

    // Create a file
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonCreate,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "lUNj6w_irSk");

    // Clear for next process
    scStreamingParser->clear();

    // Create a new version for the file
    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonCreateVer,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "a7JBKiUvv5Y");
}

TEST_F(ScStreamingParserTest, ProcessPacketTreeByCmdPutNodes)
{
    std::shared_ptr<Node> rootNode = initNodes();

    std::string jsonRes = R"([["!G(P`#}",{"e":[],"fh":["G0wgRQhS:5GXvAZORkho"]}]])";
    std::string jsonCreate =
        R"({"a":[{"a":"t","st":"!G(P`#}","t":{"f":[{"h":"r14EgLRY","t":0,"a":"ZLzvJzDefwAGc8lePmEWhU0fO8ET129XDWyA92FcyDw49VI1I-Zi5oB0Fi7lsCo7T4qjpMqT7EfPpgPNs0P6Fw","k":"vN8A0kvxmC0:ypiDHHhWkrbulm5UUMFy3gODiHsoBIZ37wEbzSSQZGU","p":"Ll5VkSJZ","ts":1770366829,"u":"vN8A0kvxmC0","s":0,"i":0}]},"ou":"vN8A0kvxmC0"}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"lUNj6w_irSk"})";

    scStreamingParser->init();

    client->scsn.setScsn(0xFFFFFFFF);

    // Simulate command PutNodes
    vector<NewNode> dummyNodeList(1);
    NewNode& dummyNode = dummyNodeList.front();
    dummyNode.source = NEW_NODE;
    dummyNode.type = FILENODE;
    dummyNode.nodehandle = 10;
    dummyNode.attrstring = std::make_unique<std::string>("{}");
    dummyNode.nodekey.resize(FILENODEKEYLENGTH);
    PrnGen rng;
    rng.genblock(reinterpret_cast<byte*>(dummyNode.nodekey.data()), FILENODEKEYLENGTH);

    byte masterKey[SymmCipher::KEYLENGTH];
    rng.genblock(masterKey, SymmCipher::KEYLENGTH);
    client->key.setkey(masterKey);

    client->putnodes(rootNode->nodeHandle(),
                     VersioningOption::ClaimOldVersion,
                     std::move(dummyNodeList),
                     nullptr,
                     0,
                     false);

    bool dummy1;
    string dummy2;
    client->reqs.serverrequest(dummy1, client.get(), dummy2);
    client->reqs.serverresponse(std::move(jsonRes), client.get());

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonCreate,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "lUNj6w_irSk");
}

TEST_F(ScStreamingParserTest, ProcessPacketTreeByCmdPutNodesFailure)
{
    std::shared_ptr<Node> rootNode = initNodes();

    std::string jsonRes = R"([["!G(P`#}",{"e":[-1],"fh":["G0wgRQhS:5GXvAZORkho"]}]])";
    std::string jsonCreate =
        R"({"a":[{"a":"t","st":"!G(P`#}","t":{"f":[]},"ou":"vN8A0kvxmC0"}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"lUNj6w_irSk"})";

    scStreamingParser->init();

    client->scsn.setScsn(0xFFFFFFFF);

    // Simulate command PutNodes
    vector<NewNode> dummyNodeList(1);
    NewNode& dummyNode = dummyNodeList.front();
    dummyNode.source = NEW_NODE;
    dummyNode.type = FILENODE;
    dummyNode.nodehandle = 10;
    dummyNode.attrstring = std::make_unique<std::string>("{}");
    dummyNode.nodekey.resize(FILENODEKEYLENGTH);
    PrnGen rng;
    rng.genblock(reinterpret_cast<byte*>(dummyNode.nodekey.data()), FILENODEKEYLENGTH);

    byte masterKey[SymmCipher::KEYLENGTH];
    rng.genblock(masterKey, SymmCipher::KEYLENGTH);
    client->key.setkey(masterKey);

    client->putnodes(rootNode->nodeHandle(),
                     VersioningOption::ClaimOldVersion,
                     std::move(dummyNodeList),
                     nullptr,
                     0,
                     false);

    bool dummy1;
    string dummy2;
    client->reqs.serverrequest(dummy1, client.get(), dummy2);
    client->reqs.serverresponse(std::move(jsonRes), client.get());

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonCreate,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "lUNj6w_irSk");
}

TEST_F(ScStreamingParserTest, ProcessPacketTreeMixOthers)
{
    initNodes();

    std::string json =
        R"({"a":[{"a":"t","st":"!?>Qzzr","t":{"f":[{"h":"i4om2BrJ","t":1,"a":"k8mDAq0-TfskLMyMqcxssQ","k":"vN8A0kvxmC0:cXtlV5RG0QHpBxl-sZWQxA","p":"Ll5VkSJZ","ts":1770364711,"u":"vN8A0kvxmC0","i":0}]},"ou":"vN8A0kvxmC0"}, {"a":"s2","st":"!G(<Cq+","n":"hH4SgJJZ","o":"UH4jcqAYP8o","ok":"AAAAAAAAAAAAAAAAAAAAAA","ha":"AAAAAAAAAAAAAAAAAAAAAA","u":"JoPrmG9jv98","r":1,"ts":1761823710,"p":"ShhP2ZnftRQ","op":1}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"JcnE8wc8Xz0"})";

    scStreamingParser->init();

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        json,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "JcnE8wc8Xz0");

    // Clear for next process
    scStreamingParser->clear();

    // Verify "t" packet without "ou" field
    std::string jsonWithoutOu =
        R"({"a":[{"a":"t","st":"!G(P`#}","t":{"f":[{"h":"r14EgLRY","t":1,"a":"k8mDAq0-TfskLMyMqcxssQ","k":"vN8A0kvxmC0:cXtlV5RG0QHpBxl-sZWQxA","p":"Ll5VkSJZ","ts":1770364711,"u":"vN8A0kvxmC0","i":0}]}}, {"a":"s2","st":"!G(Utm1","n":"hH4SgJJZ","o":"UH4jcqAYP8o","ok":"AAAAAAAAAAAAAAAAAAAAAA","ha":"AAAAAAAAAAAAAAAAAAAAAA","u":"JoPrmG9jv98","r":1,"ts":1761823710,"p":"ShhP2ZnftRQ","op":1}],"w":"https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ","sn":"JcnE8wc8Xz0"})";

    testFinishedProcess(scStreamingParser,
                        *client.get(),
                        jsonWithoutOu,
                        "https://g.api.mega.co.nz/wsc/WS_LMPJFp8VlKndvCVoSBQ",
                        "JcnE8wc8Xz0");
}

// To run this test case, build release load. Running with debug load, this test case will abort.
TEST_F(ScStreamingParserTest, DISABLED_ParseError)
{
    std::string json = R"({"a":[})";

    scStreamingParser->init();

    ASSERT_TRUE(scStreamingParser->process(json.c_str()) == 0);

    ASSERT_TRUE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_TRUE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_TRUE(isNodeTreeLocked(*client));

    scStreamingParser->clear();

    ASSERT_FALSE(scStreamingParser->hasStarted());
    ASSERT_FALSE(scStreamingParser->isFinished());
    ASSERT_FALSE(scStreamingParser->isFailed());
    ASSERT_FALSE(scStreamingParser->isPaused());
    ASSERT_EQ(client->actionpacketsCurrent, false);
    ASSERT_FALSE(isNodeTreeLocked(*client));
}

// SDK-6291: a catch-up replays an attribute's historical versions, and each one that differs from
// the cache expires it. The last replayed packet carries the version the cache already holds, which
// proves the cache is current and must therefore clear that expiry.
TEST_F(ScStreamingParserTest, UserAttrStaleVersionReplayKeepsAttributeValid)
{
    const char* uhB64 = "k-N5-o6LLkE";
    handle uh = 0;
    Base64::atob(uhB64, (byte*)&uh, MegaClient::USERHANDLE);
    User* u = client->finduser(uh, 1);
    ASSERT_NE(u, nullptr);

    u->setAttribute(ATTR_LAST_PURGE_ACKNOWLEDGED, "1750000000", "Vh");
    ASSERT_TRUE(u->getAttribute(ATTR_LAST_PURGE_ACKNOWLEDGED)->isValid());

    scStreamingParser->init();
    ASSERT_NO_FATAL_FAILURE(processUserAttrPacket(uhB64, "^!lpack", "Vold", "Gt-009Sr-XA"));
    ASSERT_FALSE(u->getAttribute(ATTR_LAST_PURGE_ACKNOWLEDGED)->isValid());
    ASSERT_NO_FATAL_FAILURE(processUserAttrPacket(uhB64, "^!lpack", "Vh", "P-LLOv0J3u0"));

    const UserAttribute* attr = u->getAttribute(ATTR_LAST_PURGE_ACKNOWLEDGED);
    ASSERT_NE(attr, nullptr);
    EXPECT_TRUE(attr->isValid())
        << "attribute holds the confirmed version Vh but was left expired by the stale replay";
    EXPECT_EQ(attr->value(), "1750000000");
    EXPECT_EQ(attr->version(), "Vh");
}

// SDK-6291: ^!dv is cached with an empty value and version and then deliberately expired, so a
// resumption from cache can tell the attribute exists without a value yet. Matching that empty
// version proves nothing, so it must not undo the expiry.
TEST_F(ScStreamingParserTest, UserAttrEmptyVersionDoesNotHealExpiredAttribute)
{
    const char* uhB64 = "k-N5-o6LLkE";
    handle uh = 0;
    Base64::atob(uhB64, (byte*)&uh, MegaClient::USERHANDLE);
    User* u = client->finduser(uh, 1);
    ASSERT_NE(u, nullptr);

    const std::string empty;
    u->setAttribute(ATTR_DISABLE_VERSIONS, empty, empty);
    u->setAttributeExpired(ATTR_DISABLE_VERSIONS);
    ASSERT_FALSE(u->getAttribute(ATTR_DISABLE_VERSIONS)->isValid());

    scStreamingParser->init();
    ASSERT_NO_FATAL_FAILURE(processUserAttrPacket(uhB64, "^!dv", "", "Gt-009Sr-XA"));

    const UserAttribute* attr = u->getAttribute(ATTR_DISABLE_VERSIONS);
    ASSERT_NE(attr, nullptr);
    EXPECT_FALSE(attr->isValid())
        << "an empty version proves nothing and must not clear the expiry";
}
}
