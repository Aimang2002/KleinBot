
#ifndef DATABASE_H
#define DATABASE_H

#include <mutex>
#include <vector>
#include "../ConfigManager/ConfigManager.h"

extern ConfigManager &CManager;

// 图片URL
class ImageURL
{
public:
	std::map<int, std::string> image_map; // 图片URL存储
	int imgID;							  // 编号自增
public:
	ImageURL();
	void readData();
	std::string getIMG_URL(int ID);
	void saveFaceURL(std::string URL);
	void writeData();
	int getIMGURL_size();
	ImageURL &operator=(const ImageURL &image);
	~ImageURL();

private:
	std::mutex img_mutex;
};

class SongID
{
public:
	std::vector<uint64_t> wyy_vector;
	const std::string wyySongIDPath = CManager.configVariable("WYY_SONGID_PATH");

public:
	SongID();

	void readData();
	int getWyy_size();
	uint64_t getWyyID(int index);
	SongID &operator=(const SongID &sID);
	~SongID();
};

class Quotes
{
public:
	Quotes();
	void readData();
	std::string getQuotes(int index);
	int getQuotesSize();
	Quotes &operator=(const Quotes &quotes);

private:
	std::vector<std::string> Quotes_vector;
	const std::string QuotesPath = CManager.configVariable("QUOTES_PATH");
};

class AnimePictrue
{
public:
	AnimePictrue();
	bool savePictrueURL(std::string URL);
	std::string getPicturURL(int index);
	int getAnimePictrueSize();
	AnimePictrue &operator=(const AnimePictrue &AP);
	~AnimePictrue();

private:
	std::mutex __mutex;
	std::unordered_map<int, std::string> AP_unMap;
	int AP_ID;

private:
	void readData();
	void writeData(std::string URL);
};

class CNGImageURL
{
public:
	CNGImageURL();
	std::string getCURL(int index);
	int getSize();

private:
	void readData();

private:
	std::unordered_map<int, std::string> CNGURL;
};

class Database
{
public:
	ImageURL imgURL;
	SongID sID;
	Quotes qs;
	AnimePictrue AP;
	CNGImageURL CIU;

public:
	static Database *getInstance();
	// 消息检查
	void messageCheck(std::string RUL);
	// 容器校验
	void databaseEmpty();

	~Database();

private:
	static Database *instance;
	static std::mutex mutex; // 互斥锁
private:
	Database();
	Database(const Database &m) {}
	Database(const Database &&m) noexcept {}
	Database &operator=(const Database &m) { return *this; }
};

#endif // DATABASE_H