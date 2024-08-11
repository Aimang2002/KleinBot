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

克莱茵QQ机器人(下面统称<font color="green" >Klein</font>)是基于C/C++开发的QQ机器人，主要用于对接各大厂商大语言模型的API和部分优秀的开源项目(详细见下)。



# 运行

目前Klein只支持在Linux x86_64系统下，后续将会陆续适配其他系统。从release下载文件后，赋予x权限，直接运行即可。



# 第三方库

该项目使用的第三方库为：

[curl](https://github.com/curl/curl)

curl安装：

> Linux Ubuntu:apt install curl

[rapidJSON](https://github.com/Tencent/rapidjson)

rapidJSON安装:

> 源码已经嵌入到项目中





# 兼容性

Klein没有实现协议端，本身并不支持直接对接QQ，而是对接第三方插件。内部使用CQ码发送内容，所以只要支持发送CQ码的QQ插件，Klein就能够跟该插件实现对接。Klein在测试时采用的QQ插件为[go-cqhttp](https://docs.go-cqhttp.org/)，如果找不到其他更好的QQ插件，可采用go-cqhttp。





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
| 群聊回复 | <font color="gree">√</font> |     需要艾特     |
| 私聊回复 | <font color="gree">√</font> |                  |
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
|   #开启语音   | 该功能将文字转语音，开启语音回复(<font color="orange">需要开源项目GPT-SoVIST</font>) | #开启语音                                                    |
|   #关闭语音   |                         关闭语音回复                         | #关闭语音                                                    |
|   #生成图片   |                    使用大模型生成一张图片                    | #生成图片:在银河中漂流的太空战舰                             |
| #删除上条对话 |                删除Klein和发送者的上一条对话                 | #删除上条对话                                                |
|    #SD绘图    |                     对接StableDiffusion                      | #SD绘图:在雪地里的可爱少女                                   |





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



# 适配的大模型平台

|  平台  |          是否支持           |
| :----: | :-------------------------: |
| OpenAI | <font color="gree">√</font> |
| Google | <font color="red">×</font>  |
| Azure  | <font color="red">×</font>  |

PS:以上只是列举一些大模型平台，有一些本地模型推理软件(LM Studio、RWKV-Running)它的接口采用的和OpenAI一样，所以这些API也是能连接的，这些没有指定平台的模型其中的API参数在配置文件中填到“OtherChatModel”中。



# 适配的开源项目

开源项目的使用方式为API调用，适配的项目有：

[Stable Diffusion ]()

[GPT-SoVIST ](https://github.com/RVC-Boss/GPT-SoVITS)





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
| WEBSOCKET_MESSAGE_IP          | 正向WS IP                                           |
| WEBSOCKET_MESSAGE_PORT        | 正向WS 端口                                         |
| REVERSEWEBSOCKET_MESSAGE_IP   | 反向WS IP                                           |
| REVERSEWEBSOCKET_MESSAGE_PORT | 反向WS 端口                                         |
|                               |                                                     |
| WYY_SONGID_PATH               | 网易云音乐ID文件路径                                |
|                               |                                                     |
|                               |                                                     |
|                               |                                                     |
| HELP_PATH                     | #帮助 文本文件路径                                  |
| HELP_PERSONALITY_PATH         | #人格帮助 文本文件路径                              |
| PERSONALITY_PATH              | 人格目录路径                                        |
| CHATMODELS_PATH               | 模型名称注册路径                                    |
| temperature                   | ”温度“超参数，默认为1                               |
| top_p                         | 核抽样,默认为1                                      |
| frequency_penalty             | 频率惩罚，默认为0                                   |
| presence_penalty              | 存在惩罚，默认为0                                   |
| MESSAGE_SURVIVAL_TIME         | 上下文存活时间，单位是秒                            |
| IMAGE_DOWNLOAD_PATH           | 图片下载存放路径，图片分析时需要用到                |
| XXX_MODEL_API_KEY             | 请求模型的API KEY                                   |
| XXX_MODEL_ENDPOINT            | 请求模型的请求端点                                  |
| XXX_DEFAULT_MODEL             | 请求的默认模型                                      |
| STABLEDIFFUSION_ENDPOINT      | Stable Diffusion 的请求端点                         |
| DEFAULT_MODEL                 | Stable Diffusion 的默认模型，目前不填               |
| VIST_API_URL                  | GPT-SoVIST API的IP                                  |
| VIST_API_PORT                 | GPT-SoVIST API的端口                                |
| VIST_REFERVOICE_PATH          | GPT-SoVIST 的参考音频                               |
| VIST_REFERVOICE_TEXT          | GPT-SoVIST 的参考音频文本                           |
| VIST_FILE_SAVE_PATH           | GPT-SoVIST 推理后音频文件存放的位置                 |





# source目录介绍

+ help

  + 存放着”#帮助“的文本。

+ image

  + 无。

+ Model

  + 存放模型名称的文件，可以在里面声明各种模型名称。需要注意的是：当模型名称为非大平台的模型时，这些模型会被Klein判断为其他模型（使用OpenAI的方式请求）。

+ personality

  + 存储着各种人格文件，其中人格文件的编写规范如下：

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