// Token/security-adjacent pure-logic tests: Discord token user-id extraction
// (TokenUtils) and Discord captcha-challenge parsing (CaptchaResolver).
//
// All "tokens" used here are synthetic, generated at test time from numeric
// ids — never real credentials.

#include "Core/TokenUtils.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/CaptchaResolver.hpp"

#include <QJsonDocument>
#include <QTest>

using namespace Acheron::Discord; // CaptchaChallenge / CaptchaSolution
using namespace Acheron::Core;    // TokenUtils namespace + its members
using Acheron::Core::Snowflake;

namespace {

// Discord token segments are base64url without padding.
QString b64url(const QByteArray &raw)
{
    return QString::fromLatin1(
            raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// A structurally valid user token whose first segment encodes the user id.
QString makeNumericIdToken(quint64 id)
{
    return b64url(QByteArray::number(id)) + '.' + b64url("timestamp") + '.' +
           b64url("signature");
}

} // namespace

class TestTokenSecurity : public QObject
{
    Q_OBJECT
private slots:
    void numericIdTokenRoundTrips();
    void numericIdTokenWithoutBase64Padding();
    void jsonPayloadIdFallback();
    void tooFewSegmentsIsInvalid();
    void nonNumericPayloadIsInvalid();
    void emptyTokenIsInvalid();
    void captchaChallengeParsed();
    void captchaChallengeAbsent();
    void captchaChallengeMalformedJson();
};

void TestTokenSecurity::numericIdTokenRoundTrips()
{
    const quint64 id = 123456789012345678ULL;
    const QString token = makeNumericIdToken(id);
    QVERIFY(!token.isEmpty());
    QVERIFY(TokenUtils::getIdAndCheckToken(token) == Snowflake(id));
    QVERIFY(TokenUtils::getIdAndCheckToken(token).isValid());
}

void TestTokenSecurity::numericIdTokenWithoutBase64Padding()
{
    // Short ids encode to a segment whose length is not a multiple of four;
    // getIdAndCheckToken must re-add the missing '=' padding before decoding.
    const quint64 id = 12345;
    const QString token = makeNumericIdToken(id);
    QVERIFY(token.split('.').size() == 3);
    QVERIFY(TokenUtils::getIdAndCheckToken(token) == Snowflake(id));
}

void TestTokenSecurity::jsonPayloadIdFallback()
{
    // Some token formats keep the id in a JSON payload in the second segment;
    // the extractor falls back to it when the first segment is not numeric.
    const QString json = QStringLiteral("{\"id\":\"876543210987654321\"}");
    const QString token = b64url("payload") + '.' + b64url(json.toUtf8()) + '.' + b64url("sig");
    QVERIFY(TokenUtils::getIdAndCheckToken(token) == Snowflake(876543210987654321ULL));
}

void TestTokenSecurity::tooFewSegmentsIsInvalid()
{
    QVERIFY(TokenUtils::getIdAndCheckToken(QStringLiteral("abc.def")) == Snowflake::Invalid);
}

void TestTokenSecurity::nonNumericPayloadIsInvalid()
{
    // Three segments, but neither segment holds a parseable user id.
    const QString token = b64url("not-a-number") + '.' + b64url("not-json") + '.' + b64url("sig");
    QVERIFY(TokenUtils::getIdAndCheckToken(token) == Snowflake::Invalid);
}

void TestTokenSecurity::emptyTokenIsInvalid()
{
    QVERIFY(TokenUtils::getIdAndCheckToken(QString()) == Snowflake::Invalid);
    QVERIFY(TokenUtils::getIdAndCheckToken(QStringLiteral("....")) == Snowflake::Invalid);
}

void TestTokenSecurity::captchaChallengeParsed()
{
    const QByteArray body = R"({
        "captcha_key": ["captcha-required"],
        "captcha_service": "hcaptcha",
        "captcha_sitekey": "abc-123",
        "captcha_session_id": "sess-1",
        "captcha_rqdata": "rq-1",
        "captcha_rqtoken": "rt-1",
        "should_serve_invisible": true
    })";
    const auto challenge = CaptchaChallenge::fromResponseBody(body);
    QVERIFY(challenge.has_value());
    QCOMPARE(challenge->service, QStringLiteral("hcaptcha"));
    QCOMPARE(challenge->sitekey, QStringLiteral("abc-123"));
    QCOMPARE(challenge->sessionId, QStringLiteral("sess-1"));
    QCOMPARE(challenge->rqdata, QStringLiteral("rq-1"));
    QCOMPARE(challenge->rqtoken, QStringLiteral("rt-1"));
    QVERIFY(challenge->shouldServeInvisible);
}

void TestTokenSecurity::captchaChallengeAbsent()
{
    const QByteArray body = R"({"message": "you need a captcha"})";
    QVERIFY(!CaptchaChallenge::fromResponseBody(body).has_value());
}

void TestTokenSecurity::captchaChallengeMalformedJson()
{
    QVERIFY(!CaptchaChallenge::fromResponseBody(QByteArrayLiteral("{not json")).has_value());
}

QTEST_APPLESS_MAIN(TestTokenSecurity)
#include "tst_TokenSecurity.moc"
