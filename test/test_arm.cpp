#include <cryptopp/cryptlib.h>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/files.h>
#include <cryptopp/channels.h>
#include <iostream>
using namespace std;
using namespace CryptoPP;
class Secret{
    public:
        Secret(){}
        ~Secret(){}
    public:
        string tosha256(string message) {

            // HashFilter f1(sha1, new HexEncoder(new StringSink(s1)));
            // HashFilter f2(sha224, new HexEncoder(new StringSink(s2)));
            HashFilter f3(sha256, new HexEncoder(new StringSink(s3)));
            // HashFilter f4(sha512, new HexEncoder(new StringSink(s4)));

            // ChannelSwitch cs;
            // cs.AddDefaultRoute(f1);
            // cs.AddDefaultRoute(f2);
            cs.AddDefaultRoute(f3);
            // cs.AddDefaultRoute(f4);

            StringSource ss(message, true /*pumpAll*/, new Redirector(cs));

            // cout << "Message: " << message << endl;
            // cout << "SHA-1: " << s1 << endl;
            // cout << "SHA-224: " << s2 << endl;
            // cout << "SHA-256: " << s3 << endl;
            // cout << "SHA-512: " << s4 << endl;
            return s3;
        };
    private:
        SHA1 sha1; SHA224 sha224; SHA256 sha256; SHA512 sha512;
        string s1, s2, s3, s4;
        // HashFilter f3(sha256, new HexEncoder(new StringSink(s3)));
        ChannelSwitch cs;
};
int main (int argc, char* argv[])
{
    using namespace CryptoPP;
    Secret objects;
    cout << objects.tosha256("123") << endl;;
    cout << objects.tosha256("2441") << endl;
    // string message = "123456";
    // if(argc == 2 && argv[1] != NULL)
    //     message = string(argv[1]);

    // string s1, s2, s3, s4;
    // SHA1 sha1; SHA224 sha224; SHA256 sha256; SHA512 sha512;

    // HashFilter f1(sha1, new HexEncoder(new StringSink(s1)));
    // HashFilter f2(sha224, new HexEncoder(new StringSink(s2)));
    // HashFilter f3(sha256, new HexEncoder(new StringSink(s3)));
    // HashFilter f4(sha512, new HexEncoder(new StringSink(s4)));

    // ChannelSwitch cs;
    // cs.AddDefaultRoute(f1);
    // cs.AddDefaultRoute(f2);
    // cs.AddDefaultRoute(f3);
    // cs.AddDefaultRoute(f4);

    // StringSource ss(message, true /*pumpAll*/, new Redirector(cs));

    // cout << "Message: " << message << endl;
    // cout << "SHA-1: " << s1 << endl;
    // cout << "SHA-224: " << s2 << endl;
    // cout << "SHA-256: " << s3 << endl;
    // cout << "SHA-512: " << s4 << endl;
    // message = "12124124";
    // StringSource sss(message, true /*pumpAll*/, new Redirector(cs));
    // cout << "Message: " << message << endl;
    // cout << "SHA-1: " << s1 << endl;
    // cout << "SHA-224: " << s2 << endl;
    // cout << "SHA-256: " << s3 << endl;
    // cout << "SHA-512: " << s4 << endl;
    return 0; 
}