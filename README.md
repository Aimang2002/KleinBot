<p align="center">
  <a >
    <img src="https://github.com/Aimang2002/mySource/blob/main/picture/anime/output.png?raw=true" width="200" height="200" alt="go-cqhttp">
  </a>
</p><br>



<div style="text-align: center;color: #7E926E; monospace;">
  <h1>克莱茵QQ机器人</h1>
  <p style="font-style: italic;color: #000;">
    基于C/C++开发的QQ机器人
  </p>
  <br>


  <p>

# 项目介绍


克莱茵QQ机器人(下面统称<font color="green" >Klein</font>)是基于C/C++开发的QQ机器人，用于调用各大厂商的LLM(大语言模型)API和部分优秀的开源项目(详细见下)。



> 最新版本：v2.4.0 开发中



# 运行

目前Klein支持的平台有：

+ Linux
+ Windows



# 第三方库

该项目使用的第三方库为：

[curl](https://github.com/curl/curl)

[Boost](https://github.com/boostorg/boost)

[nlohmann/json](https://github.com/nlohmann/json)

> nlohmann/json源码已经嵌入到项目中







# 编译

> 提示：<font color="orange">该步骤用于对其他开发者提供介绍，如果没有定制化需求，可跳过该步骤。</font>



## 1.Windows平台

~~~bash
git clone https://github.com/Aimang2002/KleinBot.git
cd KLineBot
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
make
~~~

> 在Windows平台下，我们建议使用minGW进行编译，CMakeLists文件中链接的boost库和curl库改成自己的位置。
>
> Windows平台下需要准备证书，点击[此处](https://curl.se/docs/caextract.html)下载证书，并存放到可执行文件的目录下。



## 2.Linux平台

~~~bash
git clone https://github.com/Aimang2002/KleinBot.git
cd KLineBot
mkdir build && cd build
cmake ..
make
~~~



## 3.资源

+ 资源文件可在[releases](https://github.com/Aimang2002/KleinBot/releases)下载。





# 兼容性

Klein没有实现协议端，本身并不会直接对接QQ，而是对接第三方插件的方式。内部使用CQ码发送内容，所以只要支持发送CQ码的QQ插件，Klein就能够跟该插件实现对接。Klein在测试时采用的QQ插件为[LLOneBot](https://github.com/LLOneBot/LLOneBot)，如果找不到其他更好的QQ插件，可采用LLOneBot。





## 1.QQ API 支持

|       API        |     功能     |
| :--------------: | :----------: |
|  send_group_msg  | 发送群聊消息 |
| send_private_msg | 发送私聊消息 |





## 2.连接方式

|   连接方式    |            是否可用             |
| :-----------: | :-----------------------------: |
| 正向WebSocket | <font color="gree">可用</font>  |
| 反向WebSocket | <font color="gree">可用</font>  |
|   HTTP API    | <font color="red">不可用</font> |
| 反向HTTP POST | <font color="red">不可用</font> |





# 功能

|   功能   |          是否支持           |       备注       |
| :------: | :-------------------------: | :--------------: |
| 群聊回复 | <font color="gree">√</font> |      需要@       |
| 私聊回复 | <font color="gree">√</font> |    无特殊需求    |
| 图片发送 | <font color="gree">√</font> |   需对接大模型   |
| 语音发送 | <font color="gree">√</font> | 需对接GPT-SoVIST |
| 图片分析 | <font color="gree">√</font> |   需对接大模型   |





# 内置命令

内置命令全部以”#“开头，主要用于控制机器人的一系列行为，以下为Klein内置的命令：



## 1.公有命令

|     命令      |                           命令描述                           | 实例                                                         |
| :-----------: | :----------------------------------------------------------: | ------------------------------------------------------------ |
|     #帮助     |                     向发送者介绍操作命令                     | #帮助                                                        |
|   #歌曲推荐   |           随机推荐一首来自网易云平台的歌曲给发送者           | #歌曲推荐                                                    |
|  #轻量型人格  |                   可指定内置的轻量型人格。                   | #轻量型人格:xxx                                              |
|   #设置人格   |                       可使用内置的人格                       | #设置人格:XXX                                                |
|   #人格还原   |                将之前的人格消除，转为默认人格                | #人格还原                                                    |
|     #话题     |                    可引导Klein发送的内容                     | #话题:跟我来一场辩论                                         |
|   #重置对话   |            删除所有上下问聊天，包括之前设置的人格            | #重置对话                                                    |
|   #设置定时   |           可设置提醒，到点时Klein将发送信息给你。            | “#设置定时:2024年8月2日18:10/提醒的内容"(设置的时间必须大于当前时间） |
|   #切换模型   |             根据载入的模型名称，切换各大语言模型             | #切换模型:gpt-3.5-turbo                                      |
| #查询当前模型 |                 查询当前Kline正在调用的模型                  | #查询当前模型                                                |
|   #开启语音   | 该功能将文字转语音，开启语音回复(<font color="orange">需要启动开源项目GPT-SoVIST</font>) | #开启语音                                                    |
|     #搜歌     | 将歌曲名追加到后面，机器人会返回搜索结果（目前只支持网易云音乐） | #搜歌：rubia                                                 |
|   #模型列表   |               将列举出当前Bot支持的大语言模型                | #模型列表                                                    |





## 2.私有命令

所谓私有命令，既是只有管理员用户才能使用的命令，普通用户发送管理员命令将会被无视

|        命令        |                    命令描述                    |        示例        |
| :----------------: | :--------------------------------------------: | :----------------: |
|  #开启无障碍聊天   | 所有用户对接的大模型都将会使用上下文的方式聊天 |  #开启无障碍聊天   |
|  #关闭无障碍聊天   |          取消非管理员用户的上下文聊天          |  #关闭无障碍聊天   |
|   #刷新配置文件    |              对配置文件的重新载入              |   #刷新配置文件    |
|     #激活语音      |            允许所有用户使用语音回复            |     #激活语音      |
|     #冻结语音      |           所有用户不允许使用语音回复           |     #冻结语音      |
| \#获取服务器inet4  |           获取当前本机的所有IPv4地址           | \#获取服务器inet4  |
| \#获取服务器inet6  |           获取当前本机的所有IPv6地址           | \#获取服务器inet6  |
| \#获取服务器公网IP |              获取当前本机的公网IP              | \#获取服务器公网IP |





## 3.其他命令

除了以上命令，还有一个命令较为特殊，它没有任何的操作方式，而是在发送图片的时候自动激活，届时Klein将会对接视觉模型，然后返回分析结果。



# 适配的大模型API SDK

|  平台  |          是否支持           |
| :----: | :-------------------------: |
| OpenAI | <font color="gree">√</font> |
| Anthropic | <font color="gree">√</font> |
| Google | <font color="red">×</font>  |
| Azure  | <font color="red">×</font>  |

说明：Anthropic 当前支持文本聊天和视觉调用，暂不支持本项目的 Function Calling 工具调用；Google 和 Azure 暂不实现。其他兼容 OpenAI API 规范的本地或第三方服务，可以在模型配置中使用 `OpenAI` APIStandard 接入。



# 适配的开源项目

开源项目的使用方式为API调用和服务器内部运行两种方式，适配的项目有：

[Stable Diffusion ]()

[GPT-SoVIST ](https://github.com/RVC-Boss/GPT-SoVITS)

[Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)





# 配置文件

下面是对配置文件的参数进行解释：

| 参数名                        | 参数描述                                            |
| ----------------------------- | --------------------------------------------------- |
| CONTEXT_MAX                   | 上下文最大token数，取值范围建议在1000~99999         |
| MODEL_SIGLE_TOKEN_MAX         | 单次发送给模型的最大token数，取值范围建议在100~4999 |
| GLOBAL_VOICE                  | 是否开启语音推理（true/false）                      |
| ACCESSIBLITY_CHAT             | 是否开启无障碍聊天（true/false）                    |
| CONFIG_VERSION                | 配置文件版本                                        |
| GROUP_API                     | 通常来说不建议对其更改                              |
| PRIVATE_API                   | 通常来说不建议对其更改                              |
| QBOT_NAME                     | 机器人名称，可自定义                                |
| OPEN_GROUPCHAT_MESSAGE        | 是否开启群聊（true/false）                          |
| MANAGER_QQ                    | 管理员QQ                                            |
| BOT_QQ                        | 机器人QQ                                            |
| WEBSOCKET_MESSAGE_IP          | 正向WS IP地址                                       |
| WEBSOCKET_MESSAGE_PORT        | 正向WS 端口                                         |
| REVERSEWEBSOCKET_MESSAGE_IP   | 反向WS IP地址                                       |
| REVERSEWEBSOCKET_MESSAGE_PORT | 反向WS 端口                                         |
| WYY_SONGID_PATH               | 网易云音乐ID文件路径                                |
| HELP_PATH                     | #帮助 文本文件路径                                  |
| HELP_PERSONALITY_PATH         | #人格帮助 文本文件路径                              |
| PERSONALITY_PATH              | 人格目录路径                                        |
| CHATMODELS_PATH               | 模型名称注册路径                                    |
| temperature                   | ”温度“超参数，默认为1                               |
| top_p                         | 核抽样,默认为1                                      |
| frequency_penalty             | 频率惩罚，默认为0                                   |
| presence_penalty              | 存在惩罚，默认为0                                   |
| MESSAGE_SURVIVAL_TIME         | 上下文存活时间，单位是秒                            |
| MESSAGE_WORKER_THREADS        | 消息处理线程池大小，未配置时默认为4                 |
| CONVERSATION_DB_PATH          | 对话与长期记忆 SQLite 路径，默认 source/conversations.db |
| IMAGE_ASSET_PATH              | 图片资源目录，默认 source/image_assets                  |
| MEMORY_ENABLED                | 是否启用长期记忆，未配置时默认 true                 |
| MEMORY_MODEL                  | 长期记忆提取模型名称，未配置时使用 DEFAULT_MODEL    |
| MEMORY_BATCH_TURNS            | 累计多少轮对话后触发记忆提取，默认3                 |
| MEMORY_IDLE_SECONDS           | 会话空闲多少秒后提取未满批次的记忆，默认20          |
| MEMORY_RECALL_LIMIT           | 单次长期记忆召回上限，默认8                         |
| IMAGE_DOWNLOAD_PATH           | 图片下载存放路径，图片分析时需要用到                |
| XXX_MODEL_API_KEY             | 请求模型的API KEY                                   |
| XXX_MODEL_ENDPOINT            | 请求模型的请求端点                                  |
| XXX_DEFAULT_MODEL             | 请求的模型                                          |
| XXX_MODEL_APISTANDARD         | 模型使用的API规范                                   |
| STABLEDIFFUSION_ENDPOINT      | Stable Diffusion 的请求端点                         |
| DEFAULT_MODEL                 | Stable Diffusion 的默认模型，目前不填               |
| VIST_API_URL                  | GPT-SoVIST API的IP                                  |
| VIST_API_PORT                 | GPT-SoVIST API的端口                                |
| VIST_REFERVOICE_PATH          | GPT-SoVIST 的参考音频                               |
| VIST_REFERVOICE_TEXT          | GPT-SoVIST 的参考音频文本                           |
| VIST_FILE_SAVE_PATH           | GPT-SoVIST 推理后音频文件存放的位置                 |
| REALESGAN_PATH                | REALESGAN项目的路径                                 |
| REALESGAN_MODEL               | REALESGAN使用的修复模型                             |
| IMAGE_DOWNLOAD_PATH           | 图片下载后的位置(供REALESGAN使用)                   |


# 长期记忆

Klein 会继续完整保存原始对话，并在后台从多轮对话中提取用户资料、偏好、关系、事件、状态、决定、任务和技术事实。长期记忆使用稳定的 `memory_key` 更新同一事实，并生成包含同义表达的 `search_text`，召回时优先搜索长期记忆，未命中再回退原始历史。

长期记忆提取不会阻塞当前聊天回复。`#重置对话` 会同时清除该用户的原始历史和长期记忆，删除最近上下文时也会失效来源位于删除区间内的记忆。详细的数据结构、配置和验证方式见 `docs/long-term-memory.md`。

聊天中的图片会保存为用户隔离的资源，并在上下文中使用 `asset_id` 占位符。模型可通过工具查看历史图片、生成图片或重新发送图片。`#重置对话` 和删除最近上下文会同步清理关联的图片文件；未关联到上下文的临时资源会保留到用户重置。


# 运行时架构

- 消息处理使用固定大小线程池，默认 4 个工作线程，可通过 `MESSAGE_WORKER_THREADS` 调整。
- 正向 WebSocket、反向 WebSocket 和定时任务线程共享统一的运行状态，收到 `SIGINT` 或 `SIGTERM` 后会停止重连、等待任务结束并依次回收资源。
- 日志后台线程在程序退出前排空日志队列并关闭日志文件。
- 长期记忆提取使用独立后台线程；程序退出时不会阻塞等待尚未触发的记忆批次。





# source目录介绍

+ help

  + 存放着”#帮助“的文本。

+ image

  + 无。

+ Model

  + 存放模型名称的文件，可以在里面声明各种模型名称，分别在里面声明模型名称和模型厂商(使用的API规范)，如下：

    ~~~json
    {
        "Models": [
            {
                "ModelName": [
                    "gpt-4",
                    "gpt-4o",
                    "o1-preview"
                ],
                "api_key": "sk-",
                "api_endpoint": "https://api.xxx.com/v1/chat/completions",
                "APIStandard": "OpenAI"
            },
            {
                "name": [
                  "deepseek-chat",
                    "deepseek-reasoner",
                    "deepseek-coder"
                ],
                "api_key": "sk-xxx",
                "api_endpoint": "https://api.xxx.com/v1/chat/completions",
                "APIStandard": "OpenAI"
            }
          ... 在此处添加其他模型，方式请遵循上面的格式
        ]
    }
    ~~~
    
    以api_key为一组，在改组内，所有的模型将会使用改组的api_key和endpoint。
    
    

+ personality

  + 存储着各种人格文件，其中默认存在“personality.txt”。其他人格文件的编写规范如下：

    ~~~
    Rersonality:{人格描述}
    
    Temperture:{1}
    
    Top_p:{1}
    
    Frequency_penalty:{0}
    
    Presence_penalty:{0}
    ~~~

    需务必遵守。人格文件可以无限增加，可以添加各种人格，并且调整超参数，但需要注意的是，超参数的调整必须在了解的情况下，否则请使用默认值。

+ Song

  + 存储网易云音乐ID的文件夹

+ voice

  + 无







# 鸣谢

+ [nlohmann/json](https://github.com/nlohmann/json)
+ [Boost](https://github.com/boostorg/boost)
+ [curl](https://curl.se/)
