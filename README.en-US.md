

<p align="center">
  <a >
    <img src="https://github.com/Aimang2002/mySource/blob/main/picture/anime/output.png?raw=true" width="200" height="200" alt="go-cqhttp">
  </a>
</p><br>



<div style="text-align: center;color: #7E926E; monospace;">
  <h1>Klein QQ Bot</h1>
  <p style="font-style: italic;color: #000;">
    QQ Bot developed with C/C++
  </p>
  <br>


  <p>

# Project Introduction


Klein QQ Bot (hereinafter referred to as <font color="green" >Klein</font>) is a QQ bot developed with C/C++, designed to call LLM (Large Language Model) APIs from various vendors and integrate with several excellent open-source projects (details below).



> Latest Version: v2.4.0 (In Development)



# Running

Klein currently supports the following platforms:

+ Linux
+ Windows



# Third-Party Libraries

The third-party libraries used in this project are:

[curl](https://github.com/curl/curl)

[Boost](https://github.com/boostorg/boost)

[nlohmann/json](https://github.com/nlohmann/json)

> The source code of nlohmann/json has already been embedded into the project.




# Compilation

> Note: <font color="orange">This step is intended to provide guidance for other developers. If you have no customization requirements, you can skip this step.</font>



## 1. Windows Platform

~~~bash
git clone https://github.com/Aimang2002/KleinBot.git
cd KLineBot
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
make
~~~

> On the Windows platform, we recommend compiling with MinGW. Modify the paths to the Boost and curl libraries in the CMakeLists file to match your local locations.
>
> On Windows, you need to prepare certificate files. Click [here](https://curl.se/docs/caextract.html) to download the certificates and place them in the same directory as the executable file.



## 2. Linux Platform

~~~bash
git clone https://github.com/Aimang2002/KleinBot.git
cd KLineBot
mkdir build && cd build
cmake ..
make
~~~



## 3. Resources

+ Resource files can be downloaded from [releases](https://github.com/Aimang2002/KleinBot/releases).



# Compatibility

Klein does not implement the protocol layer itself and does not directly connect to QQ. Instead, it interfaces with third-party plugins. It internally uses CQ codes to send content, so it can connect with any QQ plugin that supports sending CQ codes. During testing, Klein used the [LLOneBot](https://github.com/LLOneBot/LLOneBot) QQ plugin. If you cannot find a better alternative, you can use LLOneBot.



## 1. QQ API Support

|       API        |     Function     |
| :--------------: | :--------------: |
|  send_group_msg  | Send group chat messages |
| send_private_msg | Send private chat messages |



## 2. Connection Methods

|   Connection Method    |            Available             |
| :---------------------: | :-----------------------------: |
|  Forward WebSocket | <font color="gree">Available</font>  |
|  Reverse WebSocket | <font color="gree">Available</font>  |
|   HTTP API    | <font color="red">Unavailable</font> |
|  Reverse HTTP POST | <font color="red">Unavailable</font> |



# Features

|   Feature   |          Supported           |       Notes       |
| :------: | :-------------------------: | :--------------: |
|  Group Chat Reply | <font color="gree">√</font> |      Requires @ mention       |
|  Private Chat Reply | <font color="gree">√</font> |    No special requirements    |
|  Image Sending | <font color="gree">√</font> |   Requires LLM integration   |
|  Voice Sending | <font color="gree">√</font> | Requires GPT-SoVIST integration |
|  Image Analysis | <font color="gree">√</font> |   Requires LLM integration   |



# Built-in Commands

All built-in commands start with "#". They are primarily used to control various behaviors of the bot. Below are Klein's built-in commands:



## 1. Public Commands

|     Command      |                           Description                           | Example                                                         |
| :-----------: | :----------------------------------------------------------: | ------------------------------------------------------------ |
|     #帮助     |                     Introduces operational commands to the sender                     | #帮助                                                        |
|   #歌曲推荐   |           Randomly recommends a song from NetEase Cloud Music to the sender           | #歌曲推荐                                                    |
|  #轻量型人格  |                   Allows specifying a built-in lightweight personality.                   | #轻量型人格:xxx                                              |
|   #设置人格   |                       Uses built-in personalities                       | #设置人格:XXX                                                |
|   #人格还原   |                Removes the previously set personality and reverts to the default                | #人格还原                                                    |
|     #话题     |                    Guides the content Klein will send                     | #话题:跟我来一场辩论                                         |
|   #重置对话   |            Deletes all contextual chat history, including previously set personalities            | #重置对话                                                    |
|   #设置定时   |           Sets a reminder; Klein will send you a message at the specified time.            | “#设置定时:2024年8月2日18:10/提醒的内容"(The set time must be greater than the current time) |
|   #切换模型   |             Switches between large language models based on the loaded model name             | #切换模型:gpt-3.5-turbo                                      |
| #查询当前模型 |                 Checks the model currently being called by Klein                  | #查询当前模型                                                |
|   #开启语音   | Converts text to speech and enables voice replies (<font color="orange">requires running the open-source project GPT-SoVIST</font>) | #开启语音                                                    |
|     #搜歌     | Appends the song name, and the bot returns search results (currently only supports NetEase Cloud Music) | #搜歌：rubia                                                 |
|   #模型列表   |               Lists all large language models currently supported by the bot                | #模型列表                                                    |



## 2. Private Commands

Private commands are those that can only be used by administrator users. If regular users send administrator commands, they will be ignored.

|        Command        |                    Description                    |        Example        |
| :----------------: | :--------------------------------------------: | :----------------: |
|  #开启无障碍聊天   | All LLMs used by all users will utilize contextual chat |  #开启无障碍聊天   |
|  #关闭无障碍聊天   |          Disables contextual chat for non-admin users          |  #关闭无障碍聊天   |
|   #刷新配置文件    |              Reloads the configuration file              |   #刷新配置文件    |
|     #激活语音      |            Allows all users to use voice replies            |     #激活语音      |
|     #冻结语音      |           Disables voice replies for all users           |     #冻结语音      |
| \#获取服务器inet4  |           Retrieves all IPv4 addresses of the current machine           | \#获取服务器inet4  |
| \#获取服务器inet6  |           Retrieves all IPv6 addresses of the current machine           | \#获取服务器inet6  |
| \#获取服务器公网IP |              Retrieves the public IP of the current machine              | \#获取服务器公网IP |



## 3. Other Commands

In addition to the above commands, there is one more special command. It has no explicit invocation method; instead, it activates automatically when an image is sent. At that time, Klein will interface with a vision model and return the analysis results.



# Supported Large Model API SDKs

|  Platform  |          Supported           |
| :----: | :-------------------------: |
| OpenAI | <font color="gree">√</font> |
| Google | <font color="red">×</font>  |
| Azure  | <font color="red">×</font>  |

PS: The above only lists some major model platforms. Some local model inference software (e.g., LM Studio, RWKV-Running) adopts the OpenAI SDK interface specification, so these APIs can also be connected. For models not tied to a specific platform, their API parameters should be filled into the "OtherChatModel" section in the configuration file.



# Supported Open-Source Projects

Open-source projects can be utilized via API calls or running internally on the server. Supported projects include:

[Stable Diffusion ]()

[GPT-SoVIST ](https://github.com/RVC-Boss/GPT-SoVITS)

[Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)




# Configuration File

Below is an explanation of the configuration file parameters:

| Parameter Name                        | Description                                            |
| ----------------------------- | --------------------------------------------------- |
| CONTEXT_MAX                   | Maximum number of tokens for context; recommended range is 1000~99999         |
| MODEL_SIGLE_TOKEN_MAX         | Maximum number of tokens sent to the model per request; recommended range is 100~4999 |
| GLOBAL_VOICE                  | Whether to enable voice inference (true/false)                      |
| ACCESSIBLITY_CHAT             | Whether to enable accessibility chat (true/false)                    |
| CONFIG_VERSION                | Configuration file version                                        |
| GROUP_API                     | Generally, it is not recommended to modify this                              |
| PRIVATE_API                   | Generally, it is not recommended to modify this                              |
| QBOT_NAME                     | Bot name, customizable                                |
| OPEN_GROUPCHAT_MESSAGE        | Whether to enable group chat (true/false)                          |
| MANAGER_QQ                    | Administrator QQ ID                                            |
| BOT_QQ                        | Bot QQ ID                                            |
| WEBSOCKET_MESSAGE_IP          | Forward WebSocket IP address                                       |
| WEBSOCKET_MESSAGE_PORT        | Forward WebSocket Port                                         |
| REVERSEWEBSOCKET_MESSAGE_IP   | Reverse WebSocket IP address                                       |
| REVERSEWEBSOCKET_MESSAGE_PORT | Reverse WebSocket Port                                         |
| WYY_SONGID_PATH               | NetEase Cloud Music ID file path                                |
| HELP_PATH                     | #help text file path                                  |
| HELP_PERSONALITY_PATH         | #personality_help text file path                              |
| PERSONALITY_PATH              | Personality directory path                                        |
| CHATMODELS_PATH               | Model name registration path                                    |
| temperature                   | "Temperature" hyperparameter, defaults to 1                               |
| top_p                         | Nucleus sampling, defaults to 1                                      |
| frequency_penalty             | Frequency penalty, defaults to 0                                   |
| presence_penalty              | Presence penalty, defaults to 0                                   |
| MESSAGE_SURVIVAL_TIME         | Context survival time, unit is seconds                            |
| IMAGE_DOWNLOAD_PATH           | Image download storage path, used for image analysis                |
| XXX_MODEL_API_KEY             | API KEY for requesting the model                                   |
| XXX_MODEL_ENDPOINT            | Request endpoint for the model                                  |
| XXX_DEFAULT_MODEL             | Requested model name                                          |
| XXX_MODEL_APISTANDARD         | API standard used by the model                                   |
| STABLEDIFFUSION_ENDPOINT      | Request endpoint for Stable Diffusion                         |
| DEFAULT_MODEL                 | Default model for Stable Diffusion, leave empty for now               |
| VIST_API_URL                  | IP address for GPT-SoVIST API                                  |
| VIST_API_PORT                 | Port for GPT-SoVIST API                                |
| VIST_REFERVOICE_PATH          | Reference audio for GPT-SoVIST                               |
| VIST_REFERVOICE_TEXT          | Reference audio text for GPT-SoVIST                           |
| VIST_FILE_SAVE_PATH           | Storage location for audio files after GPT-SoVIST inference                 |
| REALESGAN_PATH                | Path to the Real-ESRGAN project                                 |
| REALESGAN_MODEL               | Restoration model used by Real-ESRGAN                             |
| IMAGE_DOWNLOAD_PATH           | Location for downloaded images (for Real-ESRGAN use)                   |



# Source Directory Introduction

+ help

  + Contains the text for the `#帮助` command.

+ image

  + None.

+ Model

  + Contains files for model names. You can declare various model names here, specifying the model name and provider (API standard used) as follows:

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
          ... Add other models here, following the format above
        ]
    }
    ~~~
    
    Models are grouped by `api_key`. Within a group, all models will share the group's `api_key` and `endpoint`.
    
    

+ personality

  + Stores various personality files. By default, `personality.txt` exists. The writing specification for other personality files is as follows:

    ~~~
    Rersonality:{Personality Description}
    
    Temperture:{1}
    
    Top_p:{1}
    
    Frequency_penalty:{0}
    
    Presence_penalty:{0}
    ~~~

    Strictly adhere to this format. Personality files can be added indefinitely, allowing you to create various personalities and adjust hyperparameters. However, note that hyperparameter adjustments should only be made if you understand their effects; otherwise, use the default values.

+ Song

  + Folder storing NetEase Cloud Music IDs

+ voice

  + None




# Acknowledgments

+ [nlohmann/json](https://github.com/nlohmann/json)
+ [Boost](https://github.com/boostorg/boost)
+ [curl](https://curl.se/)
