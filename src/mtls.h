#pragma once

#include <string>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

class mTLSHandle
{
 public:
	mTLSHandle();
	~mTLSHandle();

	// ディスクリプタを持っているのでコピーコンストラクタを禁止する。
	mTLSHandle(const mTLSHandle&) = delete;
	mTLSHandle& operator=(const mTLSHandle&) = delete;

	// 初期化
	bool Init();

	// HTTPS を使うかどうかを設定する。
	// Connect() より先に設定しておくこと。
	void UseSSL(bool value) { usessl = value; }

	// 接続に使用する CipherSuites を RSA_WITH_AES_128_CBC_SHA に限定する。
	// Connect() より先に設定しておくこと。
	// XXX どういう API にすべきか
	void UseRSA();

	// アドレスファミリを指定する。デフォルトは AF_UNSPEC。
	// Connect() より先に設定しておくこと。
	void SetFamily(int family_) { family = family_; }

	// タイムアウトを設定する。デフォルトは 0 (タイムアウトしない)
	void SetTimeout(int timeout_);

	// 接続する
	bool Connect(const std::string& hostname, const std::string& servname) {
		return Connect(hostname.c_str(), servname.c_str());
	}
	bool Connect(const char *hostname, const char *servname);

	// クローズする
	void Close();

	// shutdown する
	int Shutdown(int how);

	// 読み書き
	size_t Read(void *buf, size_t len);
	size_t Write(const void *buf, size_t len);

 public:
	bool initialized {};
	bool usessl {};
	int family {};
	int timeout {};		// [msec]
	int ssl_timeout {};

	// 内部コンテキスト
	mbedtls_net_context net {};
	mbedtls_ssl_context ssl {};
	mbedtls_ssl_config conf {};

 private:
	// mbedTLS のエラーコードを文字列にして返す
	// (static バッファを使っていることに注意)
	char errbuf[128] {};
	const char *errmsg(int code);
};
