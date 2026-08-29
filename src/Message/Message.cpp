#include "../ModelApiCaller/Dock.hpp"
#include "../submodules/CloudMusicID/CloudMusicID.h"
#include "../utils/Utils.hpp"
#include "../Asset/ImageAssetStore.h"
#include "Message.h"
#include <iomanip>
#include <thread>
#include <chrono>

std::mt19937 mt_rand(1000);

Message::Message(Dock &dock, UserSessionService &userSession, ChatService &chatService,
                 MessageSenderPort &sender, ImageAssetStore &imageAssetStore,
                 CommandRegistry &registry, Voice &voice, MessageOptions options,
                 ModelEndpointOptions visionModel, bool &globalVoice)
    : dock(dock), userSession(userSession), chatService(chatService), sender(sender),
      imageAssetStore(imageAssetStore), registry(registry), voice(voice),
      options(std::move(options)), visionModel(std::move(visionModel)),
      global_Voice(globalVoice)
{
}

Message::Intent Message::classify(const InboundMessage &data)
{
	if (data.post_type != "message") // 明确为心跳包
	{
		return Intent::SystemEvent;
	}
	return Intent::Chat; // 正常聊天
}

void Message::handleMessage(const InboundMessage &current_data)
{
	Intent intent = classify(current_data);

	// 系统事件（心跳/通知/请求）：静默忽略
	if (intent == Intent::SystemEvent)
	{
		return;
	}

	// 文本消息先走命令匹配
	if (intent == Intent::Chat)
	{
		CommandContext ctx{current_data.user_id, current_data.group_id, current_data.message_type, current_data};
		auto rs = this->registry.execute(current_data.raw_message, ctx);
		if (rs.has_value())
		{
			LOG_DEBUG("识别到命令");
			dispatch(current_data, rs->payload);
			return;
		}
	}

	// 未命中命令 → LLM 对话 / 图像识别
	try
	{
		std::cout << "[" << current_data.message_type << "]" << current_data.user_id << ":" << current_data.plain_text << std::endl;

		std::string conversationText = current_data.plain_text;
		std::string inboundAssetId;
		std::optional<ChatImageContent> currentImage;
		if (!current_data.message_data_url.empty())
		{
			auto asset = this->imageAssetStore.importFromUrl(current_data.user_id,
				current_data.message_data_url, 0);
			if (asset)
			{
				inboundAssetId = asset->asset_id;
				if (!conversationText.empty())
					conversationText += "\n";
				conversationText += "[image asset_id=" + asset->asset_id + " source=inbound]";
				const std::string base64 = this->imageAssetStore.readBase64(*asset);
				currentImage = ChatImageContent{
					asset->asset_id,
					asset->mime_type.empty() ? "image/jpeg" : asset->mime_type,
					base64};
			}
		}

		// 上下文模式仅管理员：普通用户无状态单轮，不写会话也不进长期记忆
		const bool useContext = current_data.user_id == options.bot.managerId;
		ChatReply chatReply = this->chatService.reply(
			current_data.user_id, conversationText, useContext, std::move(currentImage));
		if (!inboundAssetId.empty())
			this->imageAssetStore.attachToConversation(current_data.user_id, inboundAssetId,
				chatReply.user_message_id);
		for (const auto &outbound : chatReply.outbound_messages)
			dispatch(current_data, outbound);
		std::string llm_text = chatReply.text;

		if (llm_text.empty())
		{
			return;
		}

		// 语音模式：text → TTS → VoiceMessage；否则直接 TextMessage 走分段
		if (this->global_Voice && this->userSession.isVoiceMode(current_data.user_id))
		{
			std::string audioPath = this->textToVoice(llm_text);
			if (audioPath.empty())
			{
				dispatch(current_data, TextMessage{"系统提示：语音模块异常。"});
			}
			else
			{
				dispatch(current_data, VoiceMessage{audioPath});
			}
		}
		else
		{
			dispatchText(current_data, llm_text);
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
	}
}

void Message::dispatch(const InboundMessage &data, const OutboundMessage &msg)
{
	if (data.message_type == "group")
	{
		sender.send_group(data.group_id, msg);
	}
	else
	{
		sender.send_private(data.user_id, msg);
	}
}

void Message::dispatchText(const InboundMessage &data, const std::string &text)
{
	// 群消息上限 5000 字节，私聊 4096 字符。本函数按 UTF-8 字符切分以避免截断到半个汉字
	const bool is_group = (data.message_type == "group");
	const size_t max_chars = is_group ? 5000 : 4096;

	if (text.size() <= max_chars)
	{
		dispatch(data, TextMessage{text});
		return;
	}

	LOG_WARNING("文本过长，将使用分批次发送");

	std::string remaining = text;
	auto cut_utf8_front = [](std::string &str, size_t n) -> std::string
	{
		size_t end_byte_pos = 0;
		size_t char_count = 0;
		for (size_t i = 0; i < str.length() && char_count < n;)
		{
			int len = 1;
			unsigned char c = str[i];
			if ((c & 0xF8) == 0xF0)
				len = 4;
			else if ((c & 0xF0) == 0xE0)
				len = 3;
			else if ((c & 0xE0) == 0xC0)
				len = 2;

			if (i + len > str.length())
				break;
			i += len;
			end_byte_pos = i;
			char_count++;
		}
		std::string head = str.substr(0, end_byte_pos);
		str.erase(0, end_byte_pos);
		return head;
	};

	while (!remaining.empty())
	{
		std::string chunk = cut_utf8_front(remaining, max_chars);
		dispatch(data, TextMessage{chunk});
		if (!remaining.empty())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}
}

void Message::sendError(const InboundMessage &current_data, const std::string &text)
{
	dispatch(current_data, TextMessage{text});
}

bool Message::messageFilter(std::string message_type, std::string message)
{
	// 过滤策略
	if (message_type.size() < 4)
	{
		return false;
	}

	if (!strcmp(message_type.c_str(), "group"))
	{
		if (!options.groupChatEnabled)
		{
			return false; // 群消息是否开启
		}

		if (message.find("CQ:at") == message.npos || message.find(std::to_string(options.bot.id)) == message.npos)
		{
			return false; // 过滤非AT消息
		}
	}
	// 检查CQ码，不对转发内容进行
	return true;
}

// 回调函数用于写入数据到文件
size_t write_data(void *ptr, size_t size, size_t nmemb, std::string *data)
{
	data->append(reinterpret_cast<const char *>(ptr), size * nmemb);
	return size * nmemb;
}

// 数据流转为base64编码
std::string Message::dataToBase64(const std::string &input)
{
	const std::string base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	std::string encoded;
	int val = 0;
	int bits = -6;
	const unsigned int mask = 0x3F; // 0b00111111

	for (unsigned char c : input)
	{
		val = (val << 8) + c;
		bits += 8;
		while (bits >= 0)
		{
			encoded.push_back(base64_chars[(val >> bits) & mask]);
			bits -= 6;
		}
	}

	if (bits > -6)
	{
		encoded.push_back(base64_chars[((val << 8) >> (bits + 8)) & mask]);
	}

	while (encoded.size() % 4 != 0)
	{
		encoded.push_back('=');
	}

	return encoded;
}

std::string Message::encodeToURL(const std::string &input)
{
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;

	for (char c : input)
	{
		// 保持字母数字和其他可接受的字符不变
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			escaped << c;
		}
		else if (c == ' ')
		{
			escaped << '+';
		}
		else
		{
			escaped << std::uppercase;
			escaped << '%' << std::setw(2) << int((unsigned char)c);
			escaped << std::nouppercase;
		}
	}
	return escaped.str();
}

std::string Message::provideImageRecognition(const uint64_t user_id, const std::string &message, const std::string &message_data_url)
{
	std::string conversation = message; // plain_text 已去 CQ 码，无需再切割

	// 若不存在prompt，则设置默认prompt
	if (conversation.size() < 6)
	{
		conversation = "Please analyze this picture in all aspects and answer it in Chinese";
	}

	// 初始化
	CURL *curl_handle = curl_easy_init();
	if (!curl_handle)
	{
		LOG_ERROR("curl 无法初始化。");
		return {};
	}
	CURLcode res;
	std::string input;

	// 开始执行下载操作
	if (curl_handle)
	{
		curl_easy_setopt(curl_handle, CURLOPT_URL, message_data_url.c_str());
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &input);

		res = curl_easy_perform(curl_handle);
		if (res != CURLE_OK)
		{
			LOG_ERROR("Failed to download image: " + std::string(curl_easy_strerror(res)));
			curl_easy_cleanup(curl_handle);
			return {};
		}
	}
	else
	{
		LOG_ERROR("Failed to initialize curl handle.");
		return {};
	}
	curl_easy_cleanup(curl_handle);
	LOG_INFO("图片下载完成，大小为：" + std::to_string(input.size() / 1024.0 / 1024.0) + "MB");

	// 下载完成，将数据转为base64编码
	std::string base64 = this->dataToBase64(input);


	ChatModel model;
	model.endpoint = visionModel.endpoint;
	model.api_key = visionModel.apiKey;
	model.api_standard = visionModel.apiStandard;
	const std::string &modelName = visionModel.model;

	auto response = this->dock.RequestVision(model, modelName, conversation, base64);
	std::string answer = response.content;
	std::cout << "OpenAI response: " << answer << std::endl;
	if (response.code != 200)
	{
		LOG_ERROR("OpenAI response: " + response.content);
		answer = "系统提示：分析超时，请重试。";
	}
	else
	{
		if (response.finish_reason == "length")
		{
			answer += "回复过长，可能是程序bug，请联系管理员上报bug";
		}
		else if (response.finish_reason == "content_filter")
		{
			answer = response.refusal;
		}
		return answer;
	}

	return {};
}

// 专供 textToVoice 函数的回调函数
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	std::ofstream *file = static_cast<std::ofstream *>(userp);
	if (file)
	{
		file->write(static_cast<const char *>(contents), realsize);
	}
	return realsize;
}
std::string Message::textToVoice(const std::string &text)
{
	std::string audioPath = this->voice.toAudio(text);
	if (audioPath.find(".wav") == std::string::npos)
	{
		LOG_ERROR("TTS 失败：" + audioPath);
		return {};
	}
	return audioPath;
}

Message::~Message()
{
}
