#include "../ModelApiCaller/Dock.hpp"
#include "../submodules/CloudMusicID/CloudMusicID.h"
#include "../utils/Utils.hpp"
#include "../utils/TextSplit.h"
#include "../utils/UniqueId.h"
#include "../Asset/ImageAssetStore.h"
#include "../Asset/ImagePlaceholder.h"
#include "../Application/ReplyContextRouting.h"
#include "../Application/TypingIndicator.h"
#include "../Perception/PerceptionChannel.h"
#include "../Perception/GroupContextService.h"
#include "../Affect/BaselinePolicy.h"
#include "../Affect/DecisionRecord.h"
#include "../Affect/DecisionRecorder.h"
#include "../Affect/FactsCard.h"
#include "../Affect/KnowledgeRecall.h"
#include "../Affect/PlanTask.h"
#include "../Affect/ReplyCheck.h"
#include "../Perception/GroupLineFormat.h"
#include "../Application/AtReplySituation.h"
#include "../Application/ReplyConstraints.h"
#include "../Application/ShadowDecision.h"
#include "../utils/LocalDate.h"
#include "Message.h"
#include "../Memory/PersonCard.h"
#include "../Application/NonTextLabels.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <thread>
#include <chrono>

std::mt19937 mt_rand(1000);

Message::Message(Dock &dock, UserSessionService &userSession, ChatService &chatService,
                 MessageSenderPort &sender, ImageAssetStore &imageAssetStore,
                 CommandRegistry &registry, const Live<MessageOptions> &options,
                 EndpointProvider visionModel, const Live<AffectOptions> &affectOptions,
                 TypingIndicator *typingIndicator, PerceptionChannel *perception,
                 GroupContextService *groupContext,
                 affect::DecisionRecorder *decisionRecorder,
                 ImageAssetStore *sharedAssets,
                 affect::DecisionPolicy *decisionPolicy,
                 const affect::DeployerFacts *deployerFacts,
                 affect::KnowledgeLedger *knowledgeLedger)
    : dock(dock), userSession(userSession), chatService(chatService), sender(sender),
      imageAssetStore(imageAssetStore), registry(registry),
      options(options), visionModel(std::move(visionModel)),
      typingIndicator(typingIndicator), perception(perception),
      groupContext(groupContext), decisionRecorder(decisionRecorder), sharedAssets(sharedAssets),
      decisionPolicy(decisionPolicy), affectOptions(affectOptions), deployerFacts(deployerFacts),
      knowledgeLedger(knowledgeLedger)
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

bool Message::isCommand(const InboundMessage &data) const
{
	return data.post_type == "message" && this->registry.matches(data.plain_text);
}

void Message::handleMessage(const InboundMessage &current_data, const std::string &burstNote)
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
			// 命令确认不是她的群聊发言：不进群内容缓冲、不算会话活动
			dispatch(current_data, rs->payload, true, {}, false);
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
		const bool isGroup = current_data.message_type == "group";
		if (!current_data.message_data_url.empty())
		{
			std::optional<ImageAsset> asset;
			if (isGroup && this->sharedAssets != nullptr)
			{
				// 群聊（ADR-019）：图片进群资产库（24h，群内任何人可引用），不进发送者
				// 个人库。观察路径通常已先入库，按消息 ID 复用，避免重复下载
				asset = this->sharedAssets->findBySourceMessage(current_data.group_id,
					current_data.message_id);
				if (!asset)
					asset = this->sharedAssets->importFromUrl(current_data.group_id,
						current_data.message_data_url, current_data.message_id);
			}
			else
			{
				asset = this->imageAssetStore.importFromUrl(current_data.user_id,
					current_data.message_data_url, 0);
				if (asset)
					inboundAssetId = asset->asset_id; // 仅个人库资产挂到会话消息上
			}
			if (asset)
			{
				if (!conversationText.empty())
					conversationText += "\n";
				conversationText += formatImagePlaceholder(asset->asset_id, "inbound");
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
		// 带了视频/文件/卡片等无正文段：把标签给模型，而不是谎称"没有附带任何文字"
		// （渲染与群缓冲共用 Application/NonTextLabels.h；@ 路径历来不读表情摘要）
		const std::vector<std::string> labels = nonTextLabels(current_data, /*includeFaces=*/false);
		if (conversationText.empty())
		{
			conversationText = labels.empty()
				? "（对方只是@了你，没有附带任何文字）"
				: "（对方@了你，没有文字，附带了：" + joinLabels(labels) + "）";
		}
		else
		{
			appendLabels(conversationText, labels);
		}

		// 群上下文注入（T7b）：内容层优先——按触发句相关性选择的原文块/摘要
		// （并行话题、他人反驳由此天然入选）；空选时退回关键词热点注记（T7）。
		// 数据注记只进请求副本（requestContext），不写会话历史、
		// 不参与记忆召回/提取；管理员前缀缓存仍保持逐字稳定
		std::string requestContext;
		// 取用的“现场的人”（ADR-029）：群背景里入选的群友；触发句的发送者另加
		std::vector<std::uint64_t> presentPeople;
		if (current_data.message_type == "group")
		{
			// @ 硬激活（意志层 v2）：回答走下方既有回复路径；话题会话的
			// 开启/合并由 EngagementService 决定
			if (this->groupContext != nullptr)
				this->groupContext->onAtActivated(current_data.group_id, current_data.plain_text);

			if (this->groupContext != nullptr)
				requestContext = this->groupContext->assemble(current_data.group_id,
					current_data.plain_text, &presentPeople);
			if (requestContext.empty() && this->perception != nullptr)
				requestContext = this->perception->topicNoteFor(current_data.group_id);
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
		// 人物卡（ADR-032）：私聊里对方的卡片——QQ 号、昵称、是不是部署者由程序给出，其余是她自己记下的。
		// 稳定文本，进 system（卡片改了才变一次）；长期记忆关着或没接线时不加，请求逐字不变
		if (current_data.message_type != "group" && this->personCards != nullptr)
			situationNote += this->personCards->privateSection(current_data.user_id,
				current_data.nickname, std::time(nullptr), current_data.message_id);
		// 群聊收敛契约（T7b）：行为约束归 system（D13 分工）；静态常量保证
		// 管理员群聊请求的 system 前缀逐字稳定，不影响供应商缓存命中
		if (current_data.message_type == "group")
			situationNote += GroupContextService::groupConversationContract();

		// M1 影子记录：@/私聊回复路径与主动接话共用同一决策合同；
		// recorder 关闭时完全 no-op，不改变下面的实际请求。
		// R1 影子期（群聊 @ + 非基线策略 + enforce=false）：决策改在投递之后由策略产出，
		// 这里不写基线决策；请求、回复与投递与基线逐字相同（Application/ShadowDecision.h）
		std::string decisionId;
		// 记录开关问记录器（读 affect.enabled 活值，关着时不打开决策库）；用哪种策略、影子与
		// 生效的判定出自每条消息取一次的 affect 快照（配置实时生效）
		const bool recording = this->decisionRecorder != nullptr && this->decisionRecorder->enabled();
		const Live<AffectOptions>::Snapshot affectNow = this->affectOptions.get();
		affect::DecisionPolicy *const activePolicy = this->decisionPolicy != nullptr
			? &this->decisionPolicy->resolve(*affectNow) : nullptr;
		const bool policyDecides = recording && isGroup &&
			!affect::isBaselinePolicy(activePolicy);
		const bool enforcePlan = affectNow->enforce;
		const bool shadowPolicy = policyDecides && !enforcePlan;
		const bool enforcedPolicy = policyDecides && enforcePlan;
		// 事实卡（经历层 E0，ADR-026）：只在群聊、affect 开着且接了部署者事实时有。部署者事实段是
		// 稳定文本，接在群聊收敛契约之后进 system；现场事实每分钟都变，并入本轮尾注（请求副本）。
		// judge 的材料不带现场事实（它自己加“此刻”）
		const affect::FactsCard facts = isGroup
			? affect::factsCardFor(affectNow->enabled, this->deployerFacts, std::time(nullptr))
			: affect::FactsCard{};
		situationNote += facts.systemSection;
		// 取用（经历层，ADR-029）：同样只在群聊、affect 开着且接了账本时有。现场的人 = 群背景里入选的
		// 群友 + 触发句的发送者（@ 路径唯一的回复候选）。表达请求的认识段并入尾注、接在现场事实之后
		// （本轮 user 侧，绝不进 system）；judge 的那一份经 card 放在它 prompt 的尾部
		if (isGroup && current_data.user_id != 0)
			presentPeople.push_back(current_data.user_id);
		const affect::KnowledgeCard knowledge = isGroup
			? affect::knowledgeCardFor(affectNow->enabled, this->knowledgeLedger,
				{current_data.group_id, presentPeople})
			: affect::KnowledgeCard{};
		std::string tailNote = facts.sceneNote.empty() ? burstNote
			: (burstNote.empty() ? facts.sceneNote : burstNote + "\n" + facts.sceneNote);
		// 人物卡（ADR-032）：@ 她的人当场建卡；在场的人（触发句发送者 + 群背景里入选的群友）各一行，
		// 接在现场事实之后、她的认识之前（本轮 user 侧，绝不进 system）；判定器看同一份
		std::string peopleLines;
		if (isGroup && this->personCards != nullptr && this->personCards->enabled())
		{
			this->personCards->ensure(current_data.user_id, std::time(nullptr), current_data.message_id);
			std::vector<PersonPresent> people;
			people.push_back({current_data.user_id,
				!current_data.card.empty() ? current_data.card : current_data.nickname});
			if (this->groupContext != nullptr)
				for (const auto &[id, nickname] :
					 this->groupContext->nicknamesOf(current_data.group_id, presentPeople))
					people.push_back({id, nickname});
			peopleLines = this->personCards->groupLines(people);
			const std::string section = PersonCards::groupSection(peopleLines);
			if (!section.empty())
				tailNote += (tailNote.empty() ? "" : "\n") + section;
		}
		if (!knowledge.replySection.empty())
			tailNote += (tailNote.empty() ? "" : "\n") + knowledge.replySection;
		const std::string triggerMessageId = !current_data.message_id_raw.empty()
			? current_data.message_id_raw
			: std::to_string(current_data.message_id);
		// judge 只看内存材料：群背景 + 触发句；card 只含派生字段
		const auto atCard = [&]()
		{
			affect::SituationCard card = atReplySituation(
				current_data,
				this->perception != nullptr ? this->perception->topicNoteFor(current_data.group_id)
											: std::string(),
				this->chatService.toolNamesIn(ToolVenue::Group), currentLocalHour());
			card.knowledgeSection = knowledge.judgeSection;
			card.knowledgeIds = knowledge.judgeIds;
			card.peopleSection = peopleLines;
			return card;
		};
		const std::string judgeMaterial = policyDecides
			? requestContext + "\n触发句：" + conversationText
			: std::string();
		// 计划 v2 的可回应消息（经历层 E1）：@ 路径只有触发句这一条（只在内存里交给 judge 与任务句）
		std::vector<affect::ReplyCandidate> atCandidates;
		if (policyDecides && !utils::trim(current_data.plain_text).empty())
			atCandidates.push_back({triggerMessageId,
				!current_data.card.empty() ? current_data.card : current_data.nickname,
				replyQuoteOf(current_data.plain_text), current_data.plain_text});
		// R1.c 生效期：judge 前置、决策在回复前记（与基线顺序一致）；动作只可能是 reply
		//（越界已在 JudgePolicy 回退），计划只约束分寸与工具
		ReplyConstraints constraints;
		std::string plannedAction;
		// 计划 v2 生效（经历层 E1）：不追加服务契约、尾插本轮任务句、回复经结构核对（ChatService 内
		// 重说一次）；judge 回退时是 v1 基线计划，照 R1 的生效路径
		bool planInEffect = false;
		if (enforcedPolicy)
		{
			const affect::SituationCard card = atCard();
			const affect::Decision decision = activePolicy->decideFor(card, judgeMaterial, atCandidates);
			plannedAction = decision.plan.action;
			constraints = fromPlan(decision.plan);
			planInEffect = decision.plan.isV2();
			const std::string task = affect::planTaskSentence(decision.plan,
				affect::findCandidate(atCandidates, decision.plan.replyTo));
			if (planInEffect && !task.empty())
				constraints.taskNote = "本轮任务：" + task;
			decisionId = this->decisionRecorder->recordDecision(affect::makePolicyDecision(
				"at_reply", current_data.group_id, current_data.user_id, triggerMessageId,
				decision, *activePolicy, card));
		}
		else if (recording && !shadowPolicy)
		{
			affect::DecisionRecord decision = affect::makeBaselineDecision(
				current_data.message_type == "group" ? "at_reply" : "private_reply",
				current_data.message_type == "group" ? current_data.group_id : 0,
				current_data.user_id, triggerMessageId, "reply");
			decisionId = this->decisionRecorder->recordDecision(std::move(decision));
		}
		// 她有拒绝的权利（2026-11-22 部署者定规）：被 @ 时判定器可以选 no_reply。生效期就不请求表达模型、
		// 不发任何东西，执行记录 skipped（这条 @ 不写进她与对方的私聊会话；群缓冲里照样有）。想不想答由她
		//（判定器 + 她学到的认识）定，内置提示里没有“你可以不回答”这类话；回想时这也是一次经历
		if (enforcedPolicy && plannedAction == "no_reply")
		{
			affect::ExecutionRecord declined;
			declined.executionId = affect::DecisionRecorder::newExecutionId();
			declined.decisionId = decisionId;
			declined.status = "skipped";
			this->decisionRecorder->recordExecution(declined);
			LOG_INFO("被 @ 时她决定不回应（群 " + std::to_string(current_data.group_id) + "）");
			return;
		}

		// 私聊 LLM 调用前触发"正在输入"（能力位门控在 TypingIndicator 内部，fire-and-forget）
		if (typingIndicator != nullptr && current_data.message_type != "group")
			typingIndicator->begin(current_data.user_id);
		// 共享作用域（ADR-019）：群聊传群号，图片类工具据此先查群资产库
		ChatReply chatReply = this->chatService.reply(
			current_data.user_id, conversationText, std::move(currentImage),
			situationNote, requestContext, isGroup ? current_data.group_id : 0,
			enforcedPolicy ? &constraints : nullptr, tailNote);
		std::string llm_text = chatReply.text;
		// 结构核对的最后一道（经历层 E1）：ChatService 的错误串（"系统提示：…"）不是她的话，计划 v2
		// 生效时不发；她自己的回复已在 ChatService 里核对过，偏差随回复带回
		std::string checkDeviation = chatReply.checkDeviation;
		if (planInEffect && !llm_text.empty() && affect::looksLikeSystemText(llm_text))
		{
			LOG_WARNING("结构核对：@ 回复是程序提示，不发出（群 " +
						std::to_string(current_data.group_id) + "）");
			checkDeviation = "reply_check:system_text";
			llm_text.clear();
		}
		// 群聊收敛契约（T7b）：模型判定无增量时输出 [不回应] 标记，静默不发
		const bool textSuppressed = current_data.message_type == "group" &&
			!llm_text.empty() && GroupContextService::isSuppressed(llm_text);
		const bool textWillSend = !llm_text.empty() && !textSuppressed;

		// 执行记录先落行、后投递（R0.2）：协议端答复经回执把 queued 推进为
		// sent/failed/unknown 并回填真实 message_id，行必须先存在。
		// 影子期由 recordShadowAroundDelivery 在投递前落这一行（引用预分配的 decision_id）
		std::string executionId;
		affect::ExecutionRecord execution;
		if (!decisionId.empty() || shadowPolicy)
		{
			executionId = affect::DecisionRecorder::newExecutionId();
			execution.executionId = executionId;
			execution.decisionId = decisionId;
			execution.status = (!textWillSend && chatReply.outbound_messages.empty())
				? "skipped" : "queued";
			execution.actualText = planInEffect ? llm_text : chatReply.text;
			// 生效期：她没有开口（[不回应] 或空回复）即与 reply 计划不符
			const char *const actualAction =
				(textWillSend || !chatReply.outbound_messages.empty()) ? "reply" : "no_reply";
			if (enforcedPolicy && plannedAction != actualAction)
				execution.deviationReason = std::string("model_override:") + actualAction;
			if (!checkDeviation.empty())
				execution.deviationReason +=
					(execution.deviationReason.empty() ? "" : ";") + checkDeviation;
			if (!shadowPolicy)
				this->decisionRecorder->recordExecution(execution);
		}
		// 回执关联键：一轮只给一条出站——有文本回复给文本（主回复），否则给第一条
		// 出站消息。有记录器时 = execution_id；否则仅群聊需要（自消息 message_id 回填）
		std::string correlationPending = !executionId.empty()
			? executionId
			: (current_data.message_type == "group" && this->groupContext != nullptr
				? utils::uniqueId("dlv") : std::string());
		const auto deliver = [&]()
		{
			// 她这一轮确实开口了（文字或工具出站）：报给跟进那边记一句应答（ADR-024），
			// 一轮只报一次，分条发送不重复计
			if (isGroup && this->groupContext != nullptr &&
				(textWillSend || !chatReply.outbound_messages.empty()))
				this->groupContext->onAtAnswered(current_data.group_id);
			if (!inboundAssetId.empty())
				this->imageAssetStore.attachToConversation(current_data.user_id, inboundAssetId,
					chatReply.user_message_id);
			// 本轮第一条出站才带引用+@：被点名时第一句回应引用原消息，
			// 后续分条/多消息作为普通群消息直接发，避免每条都重复指向同一条 @
			bool replyContextPending = true;
			for (const auto &outbound : chatReply.outbound_messages)
			{
				dispatch(current_data, outbound, replyContextPending,
						 textWillSend ? std::string() : correlationPending);
				replyContextPending = false;
				if (!textWillSend)
					correlationPending.clear();
			}

			if (llm_text.empty())
			{
				return;
			}

			// 抑制一律留日志：每次沉默都是可观测的决策，prompt 分寸靠它调
			if (textSuppressed)
			{
				LOG_INFO("群聊收敛：本轮判定无增量，静默不回应（群 " +
						 std::to_string(current_data.group_id) + "）");
				return;
			}

			// 文字回复。要用语音时她会调用 send_voice：语音是工具出站消息，已在上面随
			// outbound_messages 发出（合成失败时那条出站就是原话文字），这里不再有语音分支
			dispatchText(current_data, llm_text, replyContextPending, correlationPending);
		};

		if (shadowPolicy)
		{
			recordShadowAroundDelivery(*this->decisionRecorder, *activePolicy, atCard(),
				judgeMaterial, triggerMessageId, std::move(execution), deliver, atCandidates);
		}
		else
		{
			deliver();
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
	}
}

void Message::dispatch(const InboundMessage &data, const OutboundMessage &msg,
					   bool withReplyContext, const std::string &correlationId,
					   bool observeOutbound)
{
	OutboundDelivery delivery;
	delivery.correlation_id = correlationId;
	if (data.message_type == "group")
	{
		delivery.target = GroupMessageTarget{std::to_string(data.group_id)};
		// 她的出站也是群内容（T7b）：入库后她下次被 @ 能接上自己说过的话；
		// 关联键随行入库，回执到达后补真实 message_id（R0.2）。
		// 命令确认/系统提示不是对话（observeOutbound=false）
		if (observeOutbound && this->groupContext != nullptr)
			this->groupContext->recordOutbound(data.group_id, msg, correlationId);
		// 指向性回复：本轮第一条出站被 @ 才引用+@ 回发起人；
		// 同一轮后续分条/多消息是裸消息，不再重复引用（固定行为，无配置开关）
		delivery.reply = buildReplyContext(data, options.get()->bot, withReplyContext);
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

void Message::dispatchText(const InboundMessage &data, const std::string &text,
						   bool withReplyContext, const std::string &correlationId)
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
		// 只有第一段是"回复"（引用+@）；后续段直接发到群里，
		// 避免一次多段回答把同一条 @ 消息引用多次。
		// 关联键每段都带：首段裸键驱动执行记录状态，后续段 "键#序号" 只聚合 message_id
		std::string segmentCorrelation = correlationId;
		if (index > 0 && !correlationId.empty())
			segmentCorrelation += "#" + std::to_string(index);
		dispatch(data, TextMessage{segments[index]},
				 withReplyContext && index == 0, segmentCorrelation);
		// 分条间隔：模拟真人连发节奏，也规避实现端风控
		if (index + 1 < segments.size())
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

void Message::sendError(const InboundMessage &current_data, const std::string &text)
{
	// 系统提示不是她的群聊发言
	dispatch(current_data, TextMessage{text}, true, {}, false);
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
		// 一次过滤取一次快照：群聊开关实时生效，bot 身份保持运行值（决定 9）
		const Live<MessageOptions>::Snapshot current = options.get();
		if (!current->groupChatEnabled)
		{
			return false; // 群消息是否开启
		}

		// 触发门槛：消息段里明确 @ 了 bot（@全体成员是广播，解码器不记录）
		if (std::find(data.mentioned_ids.begin(), data.mentioned_ids.end(),
					  current->bot.id) == data.mentioned_ids.end())
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


	auto response = this->dock.RequestVision(currentEndpoint(visionModel), conversation, base64);
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

Message::~Message()
{
}
