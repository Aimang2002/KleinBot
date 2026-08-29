

<p align="center">
  <img src="assets/avatar-rounded.png" width="180" height="180" alt="KleinBot" style="border-radius: 18px;">
</p><br>

<p align="center"><a href="README.md">中文（默认）</a> | <a href="README.en-US.md">English</a></p>



<div style="text-align: center;color: #7E926E; monospace;">
  <h1>KleinBot</h1>
  <p style="font-style: italic;color: #000;">
    A self-hosted multimodal QQ Agent built with C++17
  </p>
  <br>

  <p>
    <a href="#compilation"><img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="C++17"></a>
    <a href="#running"><img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey" alt="Linux and Windows"></a>
    <a href="#compatibility"><img src="https://img.shields.io/badge/protocol-OneBot%2011-green" alt="OneBot 11"></a>
    <a href="#agent-runtime-and-tools"><img src="https://img.shields.io/badge/Agent-tool%20calling-purple" alt="Agent tool calling"></a>
    <a href="#features"><img src="https://img.shields.io/badge/multimodal-application%20layer-orange" alt="Application-layer multimodal"></a>
    <a href="#license"><img src="https://img.shields.io/badge/license-MIT-yellow" alt="MIT License"></a>
  </p>


  <p>

# Project Introduction


KleinBot (hereinafter referred to as <font color="green" >Klein</font>) is an application-layer multimodal Agent built with C++17 and delivered through QQ. It connects to QQ through OneBot, orchestrates text, image, voice, and music messages, and combines LLM, vision, image-generation, and speech capabilities from different providers.

The model is responsible for understanding intent, planning actions, and selecting tools; the Harness provides context, connects external capabilities, executes tools, collects environmental feedback, and organizes model reasoning into a controlled task loop through permissions, validation, round limits, and resource boundaries.

> It is an **event-driven, bounded application-layer multimodal Agent**: QQ provides the interaction and delivery layer, while Klein's runtime handles perception, decisions, actions, and result integration.



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

| Capability | Description |
| --- | --- |
| **Agent Loop** | The model chooses which tools to call and how many rounds to use; the application executes tools, feeds results back into context, and enforces execution boundaries. |
| **Multimodal orchestration** | Vision-capable models can inspect images directly; other models use a dedicated vision path. Images are isolated per user and stored as reusable assets. |
| **Long-term memory** | User facts, preferences, and conversation history can be extracted in the background and recalled when needed. |
| **Reminders** | Natural-language reminder registration with structured time parsing, SQLite persistence, and daily or weekly repetition. |



# Built-in Commands

All built-in commands start with "#". They are primarily used to control various behaviors of the bot. Below are Klein's built-in commands:



## 1. Public Commands

|     Command      |                           Description                           | Example                                                         |
| :-----------: | :----------------------------------------------------------: | ------------------------------------------------------------ |
| `#帮助` | Introduces the available operations | `#帮助` |
| `#重置对话` | Clears the current conversation window for a fresh topic; history and long-term memory are kept and still recallable | `#重置对话` |
| `#重置上下文` | Completely deletes all conversation history, long-term memory, and images | `#重置上下文` |
| `#删除上条对话` | Rewinds the most recent conversation turn (`#rewind` / `#undo` are aliases) | `#删除上条对话` |
| `#设置人格` | Sets a custom personality description, persisted to SQLite across restarts | `#设置人格:你是一个傲娇猫娘` |
| `#人格还原` | Clears the custom personality and restores the `source/soul.md` default | `#人格还原` |
| `#切换模型` | Switches to a registered model | `#切换模型:deepseek-chat` |
| `#查询当前模型` | Shows the model currently in use | `#查询当前模型` |
| `#模型列表` | Lists all registered models | `#模型列表` |
| `#搜歌` | Searches NetEase Cloud Music and returns a music card | `#搜歌:rubia` |
| `#图片生成` | Generates and sends an image (`#生成图片` is an alias) | `#图片生成:cyberpunk city` |
| `#开启语音` / `#关闭语音` | Enables or disables voice replies for the current user | `#开启语音` |

Some public capabilities can also be triggered with natural language, without the `#` prefix. The model interprets the request and calls the corresponding tool:

| Capability | Example |
| --- | --- |
| Query the current model | “Which model are you using?” |
| Enable or disable voice replies | “Please reply with voice” / “Turn voice off” |
| Generate an image | “Draw a cyberpunk city for me” |
| Set, list, or cancel reminders | “Remind me to attend the meeting at 9 tomorrow morning” / “What reminders do I have?” |

Natural-language triggering depends on the model's ability and the relevant feature configuration. Commands such as `#重置对话`, `#重置上下文`, `#设置人格`, `#人格还原`, and `#切换模型` still require the explicit command format.



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



## 3. Image Messages

Sending an image does not require a command. If the main model supports vision, the image is sent as multimodal content; otherwise Klein stores it as a user-isolated asset and the model can inspect it through the `inspect_image` tool. Requests to generate, resend, or analyze images are handled by the corresponding model tools.


# Agent Runtime and Tools

Klein runs a standard tool-driven Agent loop: the model requests a tool, Klein validates permissions and executes it, the result and any outbound message are fed back into the context, and the model continues until it returns a final answer, a tool terminates the turn, or the round limit is reached.

`ChatService` coordinates each task, `ToolRegistry` exposes the available tool schemas, and `ToolContext` carries user, message, and session context. This application layer provides the Harness responsibilities: the model selects tools, while the runtime controls execution boundaries and integrates environmental feedback.



# Supported Large Model API SDKs

|  Platform  |          Supported           |
| :----: | :-------------------------: |
| OpenAI | <font color="gree">√</font> |
| Google | <font color="red">×</font>  |
| Azure  | <font color="red">×</font>  |

PS: The above only lists some major model platforms. Some local model inference software (e.g., LM Studio, RWKV-Running) adopts the OpenAI SDK interface specification, so these APIs can also be connected. For models not tied to a specific platform, their API parameters should be filled into the "OtherChatModel" section in the configuration file.



# Supported Open-Source Projects

Open-source projects can be utilized via API calls or running internally on the server. Supported projects include:

[GPT-SoVIST ](https://github.com/RVC-Boss/GPT-SoVITS)




# Configuration File

KleinBot reads `config.json` from the working directory at startup. On first run a placeholder skeleton is generated automatically (the Web panel comes up with it and its access token is printed in the startup log); the committed `config.example.json` is a complete template without secrets.

The root object is divided by responsibility: `bot`, `chat`, `models`, `features`, `memory`, `web_search`, `web_fetch`, `storage`, `network`, `communication`, and `webui`. Secret fields (API keys, tokens) accept either a local literal or an environment variable via `from_env`; the latter is recommended.

See `README.md` (Chinese) for the full parameter reference.


# Source Directory Introduction

The `#帮助` text is hard-coded in `src/Command/HelpText.h` and compiled into the executable (the content is final; when it does change, only that single file needs editing). TTS audio is written to the system temp directory (`/tmp/kleinbot/` on Linux, `%TEMP%\kleinbot\` on Windows) and deleted after being sent.

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
    
    

+ soul.md

  + Plain-text default persona. Users without a custom personality set via `#设置人格` get this file as their system prompt. If the file is missing, a built-in English fallback is used.




# Acknowledgments

+ [nlohmann/json](https://github.com/nlohmann/json)
+ [cpp-httplib](https://github.com/yhirose/cpp-httplib)
+ [Boost](https://github.com/boostorg/boost)
+ [curl](https://curl.se/)
+ [SQLite](https://www.sqlite.org/)
+ [GoogleTest](https://github.com/google/googletest)

# License

This project is released under the [MIT License](LICENSE). Third-party dependencies shipped in the repository (nlohmann/json, Boost headers, cpp-httplib, etc.) retain their original licenses. Release packages must include third-party license notices and must not contain user data or secrets.
