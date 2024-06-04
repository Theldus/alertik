# alertik
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-8A2BE2)](https://opensource.org/license/unlicense)
[![Build Status for Windows, Linux, and macOS](https://github.com/Theldus/alertik/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Theldus/alertik/actions/workflows/c-cpp.yml)

Alertik is a tiny syslog server and event notifier designed for MikroTik routers.

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

From this point, it seemed excessive to use Alpine, rsyslog, cURL, and shell scripts. So, I decided to write my own C program in the simplest way possible. The result is *Alertik, a single-file static binary, Docker image of just **355 kB**. It even fits in the ridiculous free space of my hAP ac^2 (1 MiB free)!* (Though I recommend using tmpfs.)

## How Does It Work? How to Use It?
The operation is quite simple: Alertik listens on the UDP port of your choice (5140 by default) and queues messages in a circular buffer. A second thread then retrieves one message at a time and checks if its substring (or regex, to be implemented) matches a predefined list of handlers. If a match is found, the handler is invoked with the message and the event timestamp. From this point, the user can send notifications to Telegram (or other services, if desired) based on these logs.

All of this is packed into a single 355kB binary, thanks to libcurl, BearSSL, and Musl.

### How to Use
Using Alertik is straightforward: simply configure your RouterOS to download the latest Docker image from [theldus/alertik:latest](https://hub.docker.com/repository/docker/theldus/alertik/tags) and set/export three environment variables:
- `TELEGRAM_BOT_TOKEN`: The token for a pre-configured Telegram bot.
- `TELEGRAM_CHAT_ID`: The chat ID where notifications will be sent.
- `TELEGRAM_NICKNAME`: The nickname you wish to be called.

<details><summary>The general procedure is similar for any Docker image (click to expand):</summary>

- Create a virtual interface for the Docker container (e.g., veth1-something)
- Create (or use an existing) bridge for the newly created interface.
- Create a small tmpfs, 5M is more than sufficient (remember: tmpfs only uses memory when needed, you won't actually use 5M of memory...)
- Configure the IP for the syslog server.
- Select the topics to be sent to the syslog server.
- Configure a mount point for the Alertik logs: /tmpfs/log -> /log
- Set the environment variables listed above.
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
# Docker environment variables configuration
/container envs
add key=TELEGRAM_BOT_TOKEN name=alertik value=<my-bot-token>
add key=TELEGRAM_CHAT_ID name=alertik value=<my-chat-id>
add key=TELEGRAM_NICKNAME name=alertik value=<my-nickname>
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

## Adding New Events
By default, Alertik monitors only WiFi connection attempts, which are reported in the log as follows:
```bash
wireless,info AA:BB:CC:DD:EE:FF@yourwifi: disconnected, unicast key exchange timeout, signal strength -77
```

Whenever there is a timeout during the key exchange, it indicates an authentication attempt with an invalid password. Alertik will notify you via Telegram, as configured in the environment variables in the previous section.

However, it is straightforward to add new events in three simple steps. For example, if you want to detect login events and send notifications for them:
```bash
system,info,account user admin logged in from 10.0.0.245 via winbox
```

1. Increment the number of events in `events.h`, as shown below:
```diff
diff --git a/events.h b/events.h
index 9167567..42d4d5f 100644
--- a/events.h
+++ b/events.h
@@ -9,7 +9,7 @@
    #include <time.h>

    #define MSG_MAX  2048
-   #define NUM_EVENTS  1
+   #define NUM_EVENTS  2
```

2. Add your event handler to the list of handlers, along with the substring to be searched:
```diff
diff --git a/events.c b/events.c
index 0eb3880..4e746f0 100644
--- a/events.c
+++ b/events.c
@@ -19,6 +19,11 @@ struct ev_handler handlers[NUM_EVENTS] = {
        .evnt_type = EVNT_SUBSTR
    },
    /* Add new handlers here. */
+   {
+       .str = "user admin logged in from",
+       .hnd = handle_admin_login,
+       .evnt_type = EVNT_SUBSTR
+   }
 };
```

3. Add your handler, which sends a notification via Telegram (since Alertik uses libcurl, you can also easily adapt the code to send GET/POST requests to any other similar service):
```c
void handle_admin_login(struct log_event *ev) {
    printf("Event message: %s\n", ev->msg);
    printf("Event timestamp: %jd\n", (intmax_t)ev->timestamp);

    if (send_telegram_notification(ev->msg) < 0) {
        log_msg("unable to send the notification!\n");
        return;
    }
}
```

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
armv6-linux-musleabi-gcc -Wall -Wextra -DUSE_FILE_AS_LOG  -DGIT_HASH=\"1536d63\"   -c -o alertik.o alertik.c
armv6-linux-musleabi-gcc -Wall -Wextra -DUSE_FILE_AS_LOG  -DGIT_HASH=\"1536d63\"   -c -o events.o events.c
armv6-linux-musleabi-gcc -no-pie --static  alertik.o events.o  -pthread -lcurl -lbearssl -o alertik
armv6-linux-musleabi-strip --strip-all alertik

$ ls -lah alertik
-rwxr-xr-x 1 david users 355K Jun  1 01:54 alertik
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

## Security Note
Running a Docker image on your router is a potential security risk, and you should not blindly trust pre-built Docker images, especially for sensitive devices like routers. For this reason, all Docker images provided in this repository are pushed to Dockerhub via GitHub Actions. This means you can audit the entire process from start to finish and ensure that the downloaded Docker images are exactly as specified.

Incidents like the libxz case should not be repeated, and manual uploads of binaries or source code should not be trusted when there are available alternatives.

## Contributing
Alertik is always open to the community and willing to accept contributions, whether with issues, documentation, testing, new features, bugfixes, typos, and etc. Welcome aboard.

## License
Alertik is a public domain project licensed under Unlicense.


[theldus/alertik:latest]: https://hub.docker.com/repository/docker/theldus/alertik/tags
[buildx]: https://github.com/docker/buildx
