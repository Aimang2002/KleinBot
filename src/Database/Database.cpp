#include "Database.h"

// 静态成员初始化
Database *Database::instance = nullptr;
std::mutex Database::mutex;

Database::Database()
{
	this->imgURL = ImageURL();
	this->sID = SongID();
	this->qs = Quotes();
	this->AP = AnimePictrue();
}

Database *Database::getInstance()
{
	std::lock_guard<std::mutex> lock(mutex);
	if (instance == nullptr)
		instance = new Database;
	return instance;
}

void Database::messageCheck(std::string URL)
{
	if (URL.find("[CQ:image") == URL.npos)
		return;
	try
	{
		URL.erase(0, URL.find_first_of('=') + 1);
		URL.erase(URL.find_first_of(','), URL.size());
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return;
	}

	this->imgURL.saveFaceURL(URL);
}

void Database::databaseEmpty()
{
	bool flag = true;
	if (imgURL.getIMGURL_size() < 1)
	{
		LOG_ERROR("Face database is empty!");
		flag = false;
	}
	if (sID.getWyy_size() < 1)
	{
		LOG_ERROR("NetaEase CloudMusic database is empty!");
		flag = false;
	}
	if (AP.getAnimePictrueSize() < 1)
	{
		LOG_ERROR("Animation database is empty!");
		flag = false;
	}
	if (CIU.getSize() < 1)
	{
		LOG_ERROR("Chinese national geography database is empty!！");
		flag = false;
	}

	if (flag)
	{
		LOG_INFO("数据库所有数据已全部加载!");
	}
}

Database::~Database()
{
	if (this->instance != nullptr)
	{
		delete this->instance;
		this->instance = nullptr;
	}
}

// --------------ImageURL ------------------
ImageURL::ImageURL()
{
	this->image_map = std::map<int, std::string>();
	this->imgID = 0;
	this->readData();
}

void ImageURL::readData()
{
	std::ifstream imgIfs(CManager.configVariable("FACEURL_PATH"));
	if (!imgIfs.is_open())
	{
		LOG_ERROR("file open failed!");
		return;
	}

	std::pair<int, std::string> ret;
	char buf;
	while (!imgIfs.eof())
	{
		imgIfs >> ret.first >> buf >> ret.second;
		this->image_map.insert(ret);
		this->imgID++;
	}
	this->imgID--; // 补偿
	imgIfs.close();
}

std::string ImageURL::getIMG_URL(int ID)
{
	if (this->imgID < ID)
		return this->image_map.begin()->second;
	return this->image_map.find(ID)->second;
}

void ImageURL::saveFaceURL(std::string URL)
{
	if (URL.size() < 1)
		return;

	// 异常处理
	try
	{
		URL.erase(0, URL.find_first_of('=') + 1);
		URL.erase(URL.find_first_of(','), URL.size());
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return;
	}

	if (URL.size() >= 30)
	{
		// 保存URL
		std::lock_guard<std::mutex> lock(this->img_mutex);
		this->imgID++;
		this->image_map.insert(make_pair(this->imgID, URL));
	}
	// 数据同步
	this->writeData();
}

void ImageURL::writeData()
{
	// SetConsoleOutputCP(UNICODE);
	std::lock_guard<std::mutex> lock(this->img_mutex);
	std::ofstream ofs(CManager.configVariable("FACEURL_PATH"), std::ios::app);
	if (!ofs.is_open())
	{
		LOG_ERROR("file open failed!");
		return;
	}
	ofs << this->imgID << "," << this->image_map[this->imgID] << std::endl;
	LOG_INFO("New face add over!");
	ofs.close();
}

ImageURL &ImageURL::operator=(const ImageURL &image)
{
	this->image_map = image.image_map;
	this->imgID = image.imgID;
	return *this;
}

inline int ImageURL::getIMGURL_size()
{
	return this->image_map.size();
}

ImageURL::~ImageURL() {}

// -------------Song id -----------------------
SongID::SongID()
{
	this->wyy_vector = std::vector<UINT64>();
	this->readData();
}

void SongID::readData()
{
	/* 读取网易云 */
	std::ifstream wyyIfs(this->wyySongIDPath);
	if (!wyyIfs.is_open())
	{
		LOG_ERROR("File open failed!");
		return;
	}

	int id = 0;
	while (!wyyIfs.eof())
	{
		wyyIfs >> id;
		this->wyy_vector.push_back(id);
	}

	wyyIfs.close();
}

int SongID::getWyy_size() { return this->wyy_vector.size(); }

UINT64 SongID::getWyyID(int index)
{
	if (index >= this->wyy_vector.size())
		return -1;
	return this->wyy_vector[index];
}

SongID &SongID::operator=(const SongID &sID)
{
	this->wyy_vector = sID.wyy_vector;
	return *this;
}

SongID::~SongID()
{
}

// ------------ Quotes -------------
Quotes::Quotes()
{
	this->Quotes_vector = std::vector<std::string>();
	this->readData();
}

void Quotes::readData()
{
	std::ifstream quotesIfs(QuotesPath);
	if (!quotesIfs.is_open())
	{
		LOG_ERROR("File open failed!");
		return;
	}

	std::string str;
	while (std::getline(quotesIfs, str))
	{
		this->Quotes_vector.push_back(str);
	}

	quotesIfs.close();
}

std::string Quotes::getQuotes(int index)
{
	if (index > this->Quotes_vector.size())
	{
		return "";
	}
	return this->Quotes_vector[index];
}

int Quotes::getQuotesSize()
{
	return this->Quotes_vector.size();
}

Quotes &Quotes::operator=(const Quotes &quotes)
{
	this->Quotes_vector = quotes.Quotes_vector;
	return *this;
}

// ------------AnimePictrue ------------------
AnimePictrue::AnimePictrue()
{
	this->AP_unMap = std::unordered_map<int, std::string>();
	this->AP_ID = 0;
	this->readData();
}

bool AnimePictrue::savePictrueURL(std::string URL)
{
	if (URL.size() < 1 || URL.find("CQ:image") == URL.npos)
		return false;
	try
	{
		URL.erase(0, URL.find_first_of('=') + 1);
		URL.erase(URL.find_first_of(','), URL.size());
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}

	if (URL.size() >= 30)
	{
		{
			std::lock_guard<std::mutex> lock(this->__mutex);
			this->AP_ID++;
			this->AP_unMap.insert(std::make_pair(this->AP_ID, URL));
			std::cout << "ID = " << this->AP_ID << "\tURL = " << this->AP_unMap[this->AP_ID] << std::endl;
		}
		this->writeData(URL);
		return true;
	}

	return false;
}

void AnimePictrue::readData()
{
	std::ifstream ifs(CManager.configVariable("ANIMEPICTRUE_PATH"));
	if (!ifs.is_open())
	{
		LOG_ERROR("file open failed!");
		return;
	}

	std::pair<int, std::string> p;
	char buf = ' ';
	while (ifs >> p.first >> buf >> p.second)
	{
		this->AP_unMap.insert(std::make_pair(this->AP_ID, p.second));
		this->AP_ID++;
	}
	ifs.close();
}

void AnimePictrue::writeData(std::string URL)
{
	std::lock_guard<std::mutex> lock(this->__mutex);
	std::ofstream ofs(CManager.configVariable("ANIMEPICTRUE_PATH"), std::ios::app);
	if (!ofs.is_open())
	{
		LOG_ERROR("file open failed!");
		return;
	}

	std::cout << "ID = " << this->AP_ID << "\tURL = " << this->AP_unMap[this->AP_ID] << std::endl;
	ofs << this->AP_ID << "," << this->AP_unMap[this->AP_ID] << std::endl;
	ofs.close();
}

std::string AnimePictrue::getPicturURL(int index)
{
	if (index >= this->AP_ID)
		index = 0;
	return this->AP_unMap[index];
}

int AnimePictrue::getAnimePictrueSize()
{
	return this->AP_unMap.size();
}

AnimePictrue &AnimePictrue::operator=(const AnimePictrue &AP)
{
	this->AP_unMap = AP.AP_unMap;
	this->AP_ID = AP.AP_ID;
	return *this;
}

AnimePictrue::~AnimePictrue()
{
}

CNGImageURL::CNGImageURL()
{
	this->CNGURL = std::unordered_map<int, std::string>();
	this->readData();
}

std::string CNGImageURL::getCURL(int index)
{
	return this->CNGURL[index];
}

inline int CNGImageURL::getSize()
{
	return this->CNGURL.size();
}

void CNGImageURL::readData()
{
	std::ifstream ifs(CManager.configVariable("CNGPICTRUE_PATH"));
	std::pair<int, std::string> p;
	char buff = ' ';
	if (!ifs.is_open())
	{
		perror("File open faild!");
		return;
	}

	while (ifs >> p.first >> buff >> p.second)
	{
		this->CNGURL.insert(p);
	}

	ifs.close();
}