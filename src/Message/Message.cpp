#include "../ModelApiCaller/Dock.hpp"
#include "../submodules/CloudMusicID/CloudMusicID.h"
#include "../utils/Utils.hpp"
#include "../utils/TextSplit.h"
#include "../Asset/ImageAssetStore.h"
#include "../Application/ReplyContextRouting.h"
#include "../Application/TypingIndicator.h"
#include "../Perception/PerceptionChannel.h"
#include "../Perception/GroupContextService.h"
#include "Message.h"
#include <algorithm>
#include <iomanip>
#include <thread>
#include <chrono>

std::mt19937 mt_rand(1000);

Message::Message(Dock &dock, UserSessionService &userSession, ChatService &chatService,
                 MessageSenderPort &sender, ImageAssetStore &imageAssetStore,
                 CommandRegistry &registry, Voice &voice, MessageOptions options,
                 ModelEndpointOptions visionModel, bool &globalVoice,
                 TypingIndicator *typingIndicator, PerceptionChannel *perception,
                 GroupContextService *groupContext)
    : dock(dock), userSession(userSession), chatService(chatService), sender(sender),
      imageAssetStore(imageAssetStore), registry(registry), voice(voice),
      options(std::move(options)), visionModel(std::move(visionModel)),
      global_Voice(globalVoice), typingIndicator(typingIndicator), perception(perception),
      groupContext(groupContext)
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
		// 命令匹配用 plain_text：群聊 raw_message 带 [CQ:at,...] 前缀，精确匹配永远失败
		auto rs = this->registry.execute(current_data.plain_text, ctx);
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

		// 群聊纯@（无文字无图片）：空 user 消息会让模型答非所问
		// （"没有收到你的消息内容"之类），换成明确的情境占位，
		// 让人格自然接住这次点名
		if (conversationText.empty())
		{
			conversationText = "（对方只是@了你，没有附带任何文字）";
		}

		// 群上下文注入（T7b）：内容层优先——按触发句相关性选择的原文块/摘要
		// （并行话题、他人反驳由此天然入选）；空选时退回关键词热点注记（T7）。
		// 数据注记走 user 尾部而非 system，管理员前缀缓存保持逐字稳定
		if (current_data.message_type == "group")
		{
			// @ 硬激活（意志层 v2）：回答走下方既有回复路径；话题会话的
			// 开启/合并由 EngagementService 决定
			if (this->groupContext != nullptr)
				this->groupContext->onAtActivated(current_data.group_id, current_data.plain_text);

			std::string contextNote;
			if (this->groupContext != nullptr)
				contextNote = this->groupContext->assemble(current_data.group_id,
					current_data.plain_text);
			if (contextNote.empty() && this->perception != nullptr)
				contextNote = this->perception->topicNoteFor(current_data.group_id);
			if (!contextNote.empty())
				conversationText += "\n" + contextNote;
		}

		// 上下文对所有用户开放（2026-09-19 缺陷修正）：每句都写入会话并进长期记忆，
		// 不再按管理员身份区分单轮/上下文

		// 新朋友第一句话（私聊、进程内首次且无历史会话）：情境注记进 system prompt
		// （行为约束归 system，约束力强于用户文本前插）。
		// takeFirstContact 提供进程内首次信号，hasChatHistory 排除重启后的老朋友
		std::string situationNote;
		if (current_data.message_type != "group" &&
			this->userSession.takeFirstContact(current_data.user_id) &&
			!this->userSession.hasChatHistory(current_data.user_id))
		{
			situationNote =
				"\n\n[系统注] 对方刚加上你好友，这是TA发来的第一句话：先自然回应对方说的内容，"
				"再顺带用一两句话把自己介绍给对方，不要生硬地报身份。";
		}
		// 群聊收敛契约（T7b）：行为约束归 system（D13 分工）；静态常量保证
		// 管理员群聊请求的 system 前缀逐字稳定，不影响供应商缓存命中
		if (current_data.message_type == "group")
			situationNote += GroupContextService::groupConversationContract();

		// 私聊 LLM 调用前触发"正在输入"（能力位门控在 TypingIndicator 内部，fire-and-forget）
		if (typingIndicator != nullptr && current_data.message_type != "group")
			typingIndicator->begin(current_data.user_id);
		ChatReply chatReply = this->chatService.reply(
			current_data.user_id, conversationText, std::move(currentImage),
			situationNote);
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

		// 群聊收敛契约（T7b）：模型判定无增量时输出 [不回应] 标记，静默不发。
		// 抑制一律留日志：每次沉默都是可观测的决策，prompt 分寸靠它调
		if (current_data.message_type == "group" &&
			GroupContextService::isSuppressed(llm_text))
		{
			LOG_INFO("群聊收敛：本轮判定无增量，静默不回应（群 " +
					 std::to_string(current_data.group_id) + "）");
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
	OutboundDelivery delivery;
	if (data.message_type == "group")
	{
		delivery.target = GroupMessageTarget{std::to_string(data.group_id)};
		// 她的出站也是群内容（T7b）：入库后她下次被 @ 能接上自己说过的话
		if (this->groupContext != nullptr)
			this->groupContext->recordOutbound(data.group_id, msg);
		// 指向性回复：被 @ 才引用+@ 回发起人（固定行为，无配置开关）
		delivery.reply = buildReplyContext(data, options.bot);
		if (delivery.reply)
		{
			const bool quoteable = delivery.reply->message_id != 0 ||
								   !delivery.reply->message_id_raw.empty();
			LOG_INFO("群聊回应元数据：message_id=" + std::to_string(data.message_id) +
					 (quoteable ? "，引用+@" : "，仅@（原消息缺ID）"));
		}
	}
	else
	{
		delivery.target = DirectMessageTarget{std::to_string(data.user_id)};
	}
	delivery.message = msg;
	sender.deliver(std::move(delivery));
}

void Message::dispatchText(const InboundMessage &data, const std::string &text)
{
	// 群消息上限 5000 字节，私聊 4096 字符。私聊整条单发（换行原样保留，
	// 仅超协议上限才按 UTF-8 字符硬切）；群聊按"一句一条"分条（真人打字习惯：
	// 换行即分条、代码块整发、空行丢弃），超长段硬切防截断
	const bool is_group = (data.message_type == "group");
	const size_t max_chars = is_group ? 5000 : 4096;

	const auto segments = is_group ? splitTextSegments(text, max_chars)
								   : splitTextWhole(text, max_chars);
	for (std::size_t index = 0; index < segments.size(); ++index)
	{
		dispatch(data, TextMessage{segments[index]});
		// 分条间隔：模拟真人连发节奏，也规避实现端风控
		if (index + 1 < segments.size())
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

void Message::sendError(const InboundMessage &current_data, const std::string &text)
{
	dispatch(current_data, TextMessage{text});
}

bool Message::messageFilter(const InboundMessage &data)
{
	// 只处理 message 事件（notice/request 已在 workingThread 分流，此处防御）
	if (data.post_type != "message")
	{
		return false;
	}

	if (data.message_type == "group")
	{
		if (!options.groupChatEnabled)
		{
			return false; // 群消息是否开启
		}

		// 触发门槛：消息段里明确 @ 了 bot（@全体成员是广播，解码器不记录）
		if (std::find(data.mentioned_ids.begin(), data.mentioned_ids.end(),
					  options.bot.id) == data.mentioned_ids.end())
		{
			return false;
		}
	}
	// 私聊直接放行
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
