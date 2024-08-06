# alertik
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-8A2BE2)](https://opensource.org/license/unlicense)
[![Build Status for Windows, Linux, and macOS](https://github.com/Theldus/alertik/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Theldus/alertik/actions/workflows/c-cpp.yml)

Alertik is a tiny syslog server and event notifier designed for MikroTik routers.

https://github.com/Theldus/alertik/assets/8294550/7963be36-268a-458e-9b79-83a466aa85be
<p align="center">
<i>Receiving Telegram notification due to failed WiFi login attempt</i>
</p>

## The Problem
MikroTik routers are impressive devices, capable of performing numerous tasks on small, compact, and affordable hardware. However, there is always room for improvement, and certain missing features can leave users feeling restricted. One such limitation is related to logging: RouterOS logs *a lot* of useful information, but it provides *no* means to trigger events based on these logs, which can be quite limiting for users.

To work around this, many users attempt to write scripts that poll the router's logs, parse the messages, and trigger events or configurations. However, this method proves to be highly unreliable, as evidenced by discussions in:
- [\[PROPOSAL\] Event driven scripting](https://forum.mikrotik.com/viewtopic.php?t=198490)
- [Executed a script on log event](https://forum.mikrotik.com/viewtopic.php?t=184330)
- [Blacklist for failed login to IPSec VPN](https://forum.mikrotik.com/viewtopic.php?t=148397)

and many others. There are two main issues with this approach:

1) **Polling:** How frequently should polling occur for a given event? Will events be missed between polling intervals?
2) **Timestamp:** Due to polling, scripts need to handle the event timestamps carefully. The script must check the dates of all events that occurred since the last check, which is not trivial because of the RouterOS log's date format, as highlighted in the RouterOS wiki:

> All messages stored in routers local memory can be printed from /log menu. Each entry contains time and date when event occurred, topics that this message belongs to and message itself. [...] If logs are printed at the same date when log entry was added, **then only time will be shown**. In example above you can see that second message was added on sep/15 current year (year is not added) and the last message was added today so only the time is displayed.

This makes it incredibly difficult to accurately analyze the logs and trigger events accordingly.

## The Solution
Instead of trying to parse RouterOS logs in an elaborate way, a smarter approach to receiving and handling logs is to use a syslog server:
- RouterOS sends messages immediately to the server, eliminating the need for any polling mechanism.
- By using a syslog server, the complete and parseable timestamp of the message can be configured by the server, if date usage is necessary.

Initially, my idea was to create a Docker image based on Alpine, using rsyslogd, bash scripts, and cURL for receiving logs, parsing them, and sending notifications. However, I noticed something interesting: the 'syslog' from RouterOS is simply UDP packets with raw strings, without any headers, protocols, or anything else—just the log string sent in a UDP packet to the configured syslog server.

From this point, it seemed excessive to use Alpine, rsyslog, cURL, and shell scripts. So, I decided to write my own C program in the simplest way possible. The result is *Alertik, a single-file static binary, Docker image of just **395 kB**. It even fits in the ridiculous free space of my hAP ac^2 (1 MiB free)!* (Though I recommend using tmpfs.)

## How Does It Work?
The operation is quite simple: Alertik listens on the UDP port of your choice (5140 by default) and queues messages in a circular buffer. A second thread then retrieves one message at a time and checks if its substring (or regex) matches a predefined list of handlers. If a match is found, the handler is invoked with the message and the event timestamp. From this point, the user can send notifications to some services (like Telegram, Slack, Discord, and etc) based on these logs.

All of this is packed into a single 395kB binary, thanks to libcurl, BearSSL, and Musl.

## Notifiers
In Alertik, notifiers are the services used to send the notifications. Each notifier can be configured to handle one or more events, and the system is designed to be extensible, allowing for the addition of more notifiers if needed.

Currently, Alertik supports the following notifiers:

- **Telegram Bot**
- **Slack WebHook**
- **Microsoft Teams WebHook**
- **Discord WebHook**
- **Generic WebHooks** (4 slots available)

Each notifier is configured via environment variables. Below is the list of environment variables required for configuring each notifier:

| Notifier                  | Environment Variable Name         | Description                                    |
|---------------------------|-----------------------------------|------------------------------------------------|
| **Telegram**              | `TELEGRAM_BOT_TOKEN`              | Token for the Telegram bot.                    |
|                           | `TELEGRAM_CHAT_ID`                | Chat ID where messages will be sent.           |
| **Slack**                 | `SLACK_WEBHOOK_URL`               | WebHook URL for Slack notifications.           |
| **Microsoft Teams**       | `TEAMS_WEBHOOK_URL`               | WebHook URL for Microsoft Teams notifications. |
| **Discord**               | `DISCORD_WEBHOOK_URL`             | WebHook URL for Discord notifications.         |
| **Generic WebHook 1**     | `GENERIC1_WEBHOOK_URL`            | URL for the first generic webhook.             |
| **Generic WebHook 2**     | `GENERIC2_WEBHOOK_URL`            | URL for the second generic webhook.            |
| **Generic WebHook 3**     | `GENERIC3_WEBHOOK_URL`            | URL for the third generic webhook.             |
| **Generic WebHook 4**     | `GENERIC4_WEBHOOK_URL`            | URL for the fourth generic webhook.            |

For Generic WebHooks, Alertik sends a POST request with the following JSON content to the configured URL:
```json
{"text": "<text to be sent>"}
```

## Environment Events
**Environment Events** offer the simplest way to configure event triggers. By setting a few environment variables, you can easily define how events should work, whether using substring matches or regex patterns. This approach provides a straightforward method for setting up events, and this section will guide you through configuring them with examples for both substring and regex matching.

### Configuration Format
The environment variables for configuring events follow this format:

```bash
export ENV_EVENTS="2"  # Maximum of 16 events (starting from 0)
export EVENT0_NOTIFIER=<notifier>  # Options: Telegram, Slack, Discord, Teams, Generic1 ... Generic4
export EVENT0_MATCH_TYPE="substr"  # or "regex"
export EVENT0_MATCH_STR="substring or regex pattern"
export EVENT0_MASK_MSG="message to be sent in case of match"
...
```

In `EVENT0_MASK_MSG`, you can use match groups (up to 32 groups, starting from 1) for custom messages. Use the `@` character to refer to these groups. For example, with a regex pattern:

```regex
ether2 link up \(speed (.+), full duplex\)
```

You can use the match group in `MASK_MSG` like this:

```bash
EVENT0_MASK_MSG="Your link ether2 is up at @1 speed"
```

To include an actual `@` character in the message, escape it by typing `@@`. For example:

```bash
EVENT0_MASK_MSG="User @1 and @2 were reported to user @@John"
```

### Examples: Substring Matching
#### `1)` **Identify Login Failures**

**Log Message:**
```
login failure for user admin
```

**Configuration:**
```bash
export EVENT0_NOTIFIER="Slack"
export EVENT0_MATCH_TYPE="substr"
export EVENT0_MATCH_STR="login failure for user admin"
export EVENT0_MASK_MSG="There is a failed login attempt for user admin"
```

#### `2)` **Identify WiFi Login Failures**

**Log Message:**
```
36:7F:7F:07:C4:B0@honeypot: disconnected, unicast key exchange timeout, signal strength -85
```

**Configuration:**
```bash
export EVENT0_NOTIFIER="Telegram"
export EVENT0_MATCH_TYPE="substr"
export EVENT0_MATCH_STR="honeypot: disconnected, unicast key exchange timeout"
export EVENT0_MASK_MSG="There is an attempt to login into your HoneyPot network!"
```

### Examples: Regex Matching
#### `1)` **Identify SSH Login Failures with User and IP Extraction**

**Log Message:**
```
login failure for user john_doe from 192.168.1.10 via ssh
```

**Configuration:**
```bash
export EVENT0_NOTIFIER="Discord"
export EVENT0_MATCH_TYPE="regex"
export EVENT0_MATCH_STR="login failure for user ([A-Za-z]+) from (\d{1,3}.*) via ssh"
export EVENT0_MASK_MSG="Alert: failed user attempt to login as @1 from @2"
```

#### `2)` **Identify Link Up with Speed Less Than 1Gbps**

**Log Message:**
```
eth0 link up (speed 100Mbps, full duplex)
```

**Configuration:**
```bash
export EVENT0_NOTIFIER="Teams"
export EVENT0_MATCH_TYPE="regex"
export EVENT0_MATCH_STR="([a-zA-Z0-9]+) link up \(speed (\d+Mbps), full duplex\)"
export EVENT0_MASK_MSG="Your interface @1 is running at @2"
```

#### `3)` **Log Connection Attempts**
To monitor and log incoming connection attempts to your network's PCs, you can configure Alertik to detect such events using a custom firewall rule and a regex pattern. Here’s a step-by-step guide on how to achieve this:

**1. Configure the Firewall Rule:**
First, set up a firewall rule on your router to log each new incoming connection to any of your machines. This rule also ensures that each source IP is added to an 'ignore' list to prevent duplicate logging for one week. Here's how you can add the rule:
```bash
/ip/firewall/filter
add action=add-src-to-address-list address-list=ignore_ip_log \
    address-list-timeout=1w chain=input comment=\
    "Log new incoming connections to any of my machines" \
    connection-nat-state="" connection-state=new in-interface=WANinterface \
    log=yes src-address-list=!ignore_ip_log
```

**2. Define the Regex Pattern**
Use the following regex pattern to match log entries for incoming connection attempts. This regex pattern extracts details from the log message, including the source and destination IP addresses and ports:
```
input: in:.*src-mac [0-9a-f:]+, proto [^,]+, ((\d{1,3}\.?)+):(\d{1,5})->((\d{1,3}\.?)+):(\d{1,5})
```

**Log Message:**
```
input: in:WANinterface out:(unknown 0), connection-state:new src-mac 18:3d:5e:79:42:a5, proto TCP (SYN), 192.0.2.1:45624->198.51.100.2:80, len 60
```

**Final Configuration:**
```bash
export EVENT0_NOTIFIER="Telegram"
export EVENT0_MATCH_TYPE="regex"
export EVENT0_MATCH_STR="input: in:.*src-mac [0-9a-f:]+, proto [^,]+, ((\d{1,3}\.?)+):(\d{1,5})->((\d{1,3}\.?)+):(\d{1,5})"
export EVENT0_MASK_MSG="The IP @1:@3 is trying to connect to your router @4:@6, please do something"
```

> [!NOTE]
> The regex used in Alertik follows the POSIX Regex Extended syntax. This syntax may vary slightly from patterns used in PCRE2/Perl and other regex implementations. For validation of patterns specifically for Alertik, you can use the regex validator at [https://theldus.github.io/alertik](https://theldus.github.io/alertik). Regex patterns that match in this tool are guaranteed to work correctly in Alertik.

## Static Events
**Static Events** offer a more complex event handling mechanism compared to Environment Events. These events are predefined in the source code of Alertik and can support advanced functionalities, such as tracking a certain number of similar events within a specified time window or handling events with specific values.

Similar to Environment Events, Static Events are configured through environment variables. However, their configuration options are more limited since their core logic is already implemented in the source code.

### Configuration
To enable and configure Static Events, use the following environment variables:

```bash
export STATIC_EVENTS_ENABLED="0,3,5..."
```
Each number in the list corresponds to a static event that will be enabled.

For each enabled event, specify the notifier to be used:

```bash
export STATIC_EVENT0_NOTIFIER=Telegram
export STATIC_EVENT3_NOTIFIER=Telegram
export STATIC_EVENT5_NOTIFIER=Slack
...
```

### Available Static Events
Currently, there is only one static event available:

- **Event 0: `handle_wifi_login_attempts`**  
  This event monitors logs for failed login attempts to any Wi-Fi network. When such attempts are detected, the event sends a report containing the Wi-Fi network name and the MAC address of the device.

Future versions of Alertik may include additional static events, and users have the option to add custom events directly in the source code.

### Adding New Static Events
Adding Static Events can be done in three simple steps. For example, if you want to detect login events and send notifications for them:

```bash
system,info,account user admin logged in from 10.0.0.245 via winbox
```

1. Increment the number of events in `events.h`, as shown below:
```diff
diff --git a/events.h b/events.h
index 49b4826..ab5f079 100644
--- a/events.h
+++ b/events.h
@@ -10,7 +10,7 @@
    #include <time.h>
 
    #define MSG_MAX  2048
-   #define NUM_EVENTS  1
+   #define NUM_EVENTS  2
```

2. Add your event handler to the list of handlers, along with the substring to be searched:
```diff
diff --git a/events.c b/events.c
index ce20e38..c289dc3 100644
--- a/events.c
+++ b/events.c
@@ -26,6 +26,7 @@ static regmatch_t pmatch[MAX_MATCHES];
 
 /* Handlers. */
 static void handle_wifi_login_attempts(struct log_event *, int);
+static void handle_admin_login(struct log_event *, int);
 struct static_event static_events[NUM_EVENTS] = {
    /* Failed login attempts. */
    {
@@ -36,6 +37,11 @@ struct static_event static_events[NUM_EVENTS] = {
        .ev_notifier_idx = NOTIFY_IDX_TELE
    },
    /* Add new handlers here. */
+   {
+       .ev_match_str  = "user admin logged in from",
+       .hnd           = handle_admin_login,
+       .ev_match_type = EVNT_SUBSTR
+   }
 };
```

3. Add your handler (since Alertik uses libcurl, you can also easily adapt the code to send GET/POST requests to any other similar service):
```c
static void handle_admin_login(struct log_event *ev, int idx_env)
{
    struct notifier *self;
    int notif_idx;

    log_msg("Event message: %s\n", ev->msg);
    log_msg("Event timestamp: %d\n", ev->timestamp);

    notif_idx = static_events[idx_env].ev_notifier_idx;
    self      = &notifiers[notif_idx];

    if (self->send_notification(self, ev->msg) < 0) {
        log_msg("unable to send the notification!\n");
        return;
    }
}
```

## Forward Mode
**Forward Mode** is designed for scenarios where an existing syslog server is already in use with RouterOS. This feature allows Alertik to forward received log messages without any modifications to a specified syslog server. This is particularly useful for integrating Alertik into an existing logging infrastructure while still benefiting from its event-triggering capabilities.

To enable Forward Mode, configure the following environment variables:

```bash
export FORWARD_HOST=<host>
export FORWARD_PORT=<port>
```

- **`FORWARD_HOST`**: Specify the IP address (IPv4 or IPv6) or domain name of the syslog server to which messages should be forwarded.
- **`FORWARD_PORT`**: Define the port number on which the syslog server is listening for incoming messages.

## Setup in RouterOS
Using Alertik is straightforward: simply configure your RouterOS to download the latest Docker image from [theldus/alertik:latest](https://hub.docker.com/repository/docker/theldus/alertik/tags) and set/export the environment variables related to the Notifiers and Environment/Static Events you want to configure.

<details><summary>The general procedure is similar for any Docker image (click to expand):</summary>

- Create a virtual interface for the Docker container (e.g., veth1-something)
- Create (or use an existing) bridge for the newly created interface.
- Create a small tmpfs, 5M is more than sufficient (remember: tmpfs only uses memory when needed, you won't actually use 5M of memory...)
- Configure the IP for the syslog server.
- Select the topics to be sent to the syslog server.
- Configure a mount point for the Alertik logs: /tmpfs/log -> /log
- Set the environment variables for Notifiers and Environment/Static Events.
- Configure the Docker registry to: `https://registry-1.docker.io`
- Finally, add the Docker image, pointing to: `theldus/alertik:latest`.

Below is the complete configuration for my environment, for reference:
```bash
# Virtual interface creation
/interface veth add address=<my-container-ip>/24 gateway=<gateway-ip> gateway6="" name=veth1-docker
# Bridge configuration
/interface bridge port add bridge=<my-bridge> interface=veth1-docker
# Create a small tmpfs
/disk add slot=tmpfs tmpfs-max-size=5000000 type=tmpfs
# Syslog server IP configuration
/system logging action add name=rsyslog remote=<my-container-ip> remote-port=5140 target=remote
# Topics to send to the syslog server
/system logging add action=rsyslog topics=info
# Configure rsyslog server
/system logging action add name=rsyslog remote=<your-container-ip> remote-port=5140 target=remote
# Mountpoint configuration
/container mounts add dst=/log name=logmount src=/tmpfs/log

# Docker environment variables configuration for Telegram/Slack/Discord/Teams and/or Generic events
/container envs
add key=TELEGRAM_BOT_TOKEN name=alertik value=<my-bot-token>
add key=TELEGRAM_CHAT_ID name=alertik value=<my-chat-id>
...

# Add some event, such as identifying login failures via SSH
/container envs
add key=EVENT0_NOTIFIER name=alertik value="Telegram"
add key EVENT0_MATCH_TYPE name=alertik value="substr"
add key EVENT0_MATCH_STR name=alertik value=="login failure for user admin"
add key EVENT0_MASK_MSG name=alertik value="There is a failed login attempt for user admin"

# Docker Hub registry configuration
/container config set registry-url=https://registry-1.docker.io tmpdir=tmpfs
```

and finally:
```bash
# Add Docker image
/container
add remote-image=theldus/alertik:latest envlist=alertik interface=veth1-docker mounts=logmount root-dir=tmpfs/alertik workdir=/
# Monitor the status
/container print
# Run the image
/container start 0
```
</details>

This might seem overwhelming, but trust me: **it is simple**.

Every step described above is the same process for any Docker image to be used on MikroTik. Therefore, I recommend getting familiar with Docker/container configurations before working with alertik. For this, there are at least three excellent videos from MikroTik on the subject:
- [Impossible, docker containers on Mikrotik? Part 1](https://www.youtube.com/watch?v=8u1PVouAGnk)
- [Docker containers on Mikrotik? Part 2: PiHole](https://www.youtube.com/watch?v=UMcJs4oyHDk)
- [Temporary container in the RAM (tmpfs) - a lifehack for low-cost MikroTik routers](https://www.youtube.com/watch?v=KO9wbarVPOk)

### Logging
Logging is the primary method for debugging Alertik. The system logs all operations, and any issues are likely to be reflected in the log file. To retrieve the log file, copy from the RouterOS to your local computer:

```bash
$ scp admin@<router-ip>:/tmpfs/log/log.txt .
```

(Detailed instructions on creating a mount-point with `tmpfs` were provided earlier.)

Although not main purpose, Alertik logs can also serve as a replacement for the default RouterOS logs, bypassing limitations such as the default message count restriction.

## Build Instructions
The easiest and recommended way to build Alertik is via the Docker image available at: [theldus/alertik:latest], compatible with armv6, armv7, and aarch64. However, if you prefer to build it manually, the process is straightforward since the toolchain setup is already fully scripted:
```bash
$ git clone https://github.com/Theldus/alertik.git
$ cd alertik/
$ toolchain/toolchain.sh "download_musl_armv6"
$ toolchain/toolchain.sh "download_build_bearssl"
$ toolchain/toolchain.sh "download_build_libcurl"

# Export the toolchain to your PATH
$ export PATH=$PATH:$PWD/toolchain/armv6-linux-musleabi-cross/bin

# Build
$ make CROSS=armv6
armv6-linux-musleabi-gcc -Wall -Wextra -O2 -DUSE_FILE_AS_LOG  -DGIT_HASH=\"4e82617\"   -c -o alertik.o alertik.c
armv6-linux-musleabi-gcc -Wall -Wextra -O2 -DUSE_FILE_AS_LOG  -DGIT_HASH=\"4e82617\"   -c -o events.o events.c
armv6-linux-musleabi-gcc -Wall -Wextra -O2 -DUSE_FILE_AS_LOG  -DGIT_HASH=\"4e82617\"   -c -o env_events.o env_events.c
armv6-linux-musleabi-gcc -Wall -Wextra -O2 -DUSE_FILE_AS_LOG  -DGIT_HASH=\"4e82617\"   -c -o notifiers.o notifiers.c
armv6-linux-musleabi-gcc -Wall -Wextra -O2 -DUSE_FILE_AS_LOG  -DGIT_HASH=\"4e82617\"   -c -o log.o log.c
armv6-linux-musleabi-gcc -Wall -Wextra -O2 -DUSE_FILE_AS_LOG  -DGIT_HASH=\"4e82617\"   -c -o syslog.o syslog.c
armv6-linux-musleabi-gcc -Wall -Wextra -O2 -DUSE_FILE_AS_LOG  -DGIT_HASH=\"4e82617\"   -c -o str.o str.c
armv6-linux-musleabi-gcc -no-pie --static  alertik.o events.o env_events.o notifiers.o log.o syslog.o str.o  -pthread -lcurl -lbearssl -o alertik
armv6-linux-musleabi-strip --strip-all alertik

$ ls -lah alertik
-rwxr-xr-x 1 david users 395K Aug  5 22:39 alertik
```

To generate the Docker image, ensure you have the [buildx] extension installed:
```bash
$ docker buildx build --platform linux/arm/v6,linux/arm/v7,linux/arm64 . --tag theldus/alertik:latest
[+] Building 6.9s (5/5) FINISHED                                               docker-container:multi-platform-builder
 => [internal] booting buildkit                                                                                   3.4s
 => => starting container buildx_buildkit_multi-platform-builder0                                                 3.4s
 => [internal] load build definition from Dockerfile                                                              0.2s
 => => transferring dockerfile: 105B                                                                              0.0s
 => [internal] load .dockerignore                                                                                 0.2s
 => => transferring context: 2B                                                                                   0.0s
 => [internal] load build context                                                                                 0.4s
 => => transferring context: 362.79kB                                                                             0.0s
 => [linux/arm/v6 1/1] COPY alertik /alertik                                                                      0.7s
WARNING: No output specified with docker-container driver. Build result will only remain in the build cache. To push result image into registry use --push or to load image into docker use --load
```

## Security Notice
Running a Docker image on your router can be a cause for concern. It is not advisable to blindly trust readily available Docker images, especially when it comes to sensitive devices like routers. With this in mind, all Docker images provided in this repository are exclusively pushed to Dockerhub via Github Actions. This means you can audit the entire process from start to finish, ensuring that the downloaded Docker images are exactly as they claim to be.

[Incidents like the one involving libxz](https://tukaani.org/xz-backdoor/) must not be repeated. Trust should not be placed in manual uploads, whether of binaries or source code, when there are available alternatives.

## Contributing
Alertik is always open to the community and willing to accept contributions, whether with issues, documentation, testing, new features, bugfixes, typos, and etc. Welcome aboard.

## License
Alertik is a public domain project licensed under Unlicense.

[theldus/alertik:latest]: https://hub.docker.com/repository/docker/theldus/alertik/tags
[buildx]: https://github.com/docker/buildx
