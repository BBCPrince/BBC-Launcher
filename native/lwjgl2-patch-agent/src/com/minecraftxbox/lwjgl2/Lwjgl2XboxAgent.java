package com.minecraftxbox.lwjgl2;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.InetSocketAddress;
import java.net.MulticastSocket;
import java.net.NetworkInterface;
import java.net.ProtocolFamily;
import java.net.SocketAddress;
import java.net.StandardProtocolFamily;
import java.net.StandardSocketOptions;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.ProtectionDomain;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

public final class Lwjgl2XboxAgent {
    private static final String WINDOWS_DISPLAY = "org/lwjgl/opengl/WindowsDisplay";
    private static final String WINDOWS_CONTEXT_IMPLEMENTATION = "org/lwjgl/opengl/WindowsContextImplementation";
    private static final String GL_CONTEXT = "org/lwjgl/opengl/GLContext";
    private static final String LAUNCH_WRAPPER = "net/minecraft/launchwrapper/Launch";
    private static final String LEGACY_MULTIPLAYER_SCREEN = "bnf";
    private static final String LEGACY_LAN_SERVER_LIST = "chg$b";
    private static final String LEGACY_LAN_DETECTOR_THREAD = "chg$a";
    private static final String LEGACY_CONNECT_SCREEN = "bkr";
    private static final String LEGACY_LAN_SERVER_LIST_DEOBF = "net/minecraft/client/network/LanServerDetector$LanServerList";
    private static final String LEGACY_LAN_DETECTOR_THREAD_DEOBF = "net/minecraft/client/network/LanServerDetector$ThreadLanServerFind";
    private static final String CONTROLLABLE_CONTROLLER_MANAGER = "com/mrcrayfish/controllable/client/ControllerManager";
    private static final String CONTROLLABLE_CONTROLLER_INPUT = "com/mrcrayfish/controllable/client/ControllerInput";
    private static final String CONTROLLABLE_GUI_EVENTS = "com/mrcrayfish/controllable/client/GuiEvents";
    private static final String SDL2_CONTROLLER_MANAGER = "uk/co/electronstudio/sdl2gdx/SDL2ControllerManager";
    private static final String SDL2_CONTROLLER = "uk/co/electronstudio/sdl2gdx/SDL2Controller";
    private static final String DISABLE_CONTROLLABLE_SDL2_PROPERTY = "controllable.xbox.disable_sdl2";
    private static final String LAN_PLAY_PROPERTY = "minecraft.xbox.lanPlay";
    private static final String LAN_LOG_PATH_PROPERTY = "minecraft.xbox.lanLogPath";
    private static final String LAN_MOTD_PROPERTY = "minecraft.xbox.lanMotd";
    private static final String LAN_MANUAL_SERVERS_PROPERTY = "minecraft.xbox.lan.manualServers";
    private static final String LAN_GROUP_ADDRESS = "224.0.2.60";
    private static final String FORWARDED_HOST_START = "[XBOXHOST]";
    private static final String FORWARDED_HOST_END = "[/XBOXHOST]";
    private static final int LAN_GROUP_PORT = 4445;
    private static final long LAN_ADVERTISE_INTERVAL_MS = 1500L;
    private static final long MANUAL_LAN_INJECT_INTERVAL_MS = 2000L;
    private static final Pattern LAN_PORT_PATTERN = Pattern.compile("(?i)(started serving|open(ed)? to lan|local game|lan).*?(\\d{4,5})");
    private static volatile Object wglSwapBuffersFunction;
    private static volatile boolean wglSwapLookupAttempted;
    private static volatile Object openGlLibrary;
    private static volatile Object openGlWglGetProcAddressFunction;
    private static int openGlResolveLogCount;
    private static final int LEGACY_KEYBOARD_EVENT_SIZE = 18;
    private static final int LEGACY_KEY_COUNT = 256;
    private static final Map<String, Object> legacyKeyboardFunctions = new HashMap<String, Object>();
    private static volatile boolean legacyKeyboardLookupFailed;
    private static volatile boolean legacyKeyboardCreated;
    private static volatile boolean legacyKeyboardReadyLogged;
    private static int legacyKeyboardFailureLogCount;
    private static volatile Object glfwLibrary;
    private static final Map<String, Object> glfwFunctions = new HashMap<String, Object>();
    private static volatile boolean glfwLookupFailed;
    private static volatile Object legacyControllableController;
    private static volatile Object legacyControllableControllerManager;
    private static final Object legacyGamepadLock = new Object();
    private static final byte[] legacyGamepadButtons = new byte[15];
    private static final float[] legacyGamepadAxes = new float[6];
    private static long legacyGamepadLastReadNanos;
    private static boolean legacyGamepadPresent;
    private static boolean legacyControllablePointerMode;
    private static boolean legacyControllablePointerToggleLatch;
    private static int legacyControllablePointerFailureLogCount;
    private static int legacyControllableFailureLogCount;
    private static volatile boolean lanBroadcasterStarted;
    private static volatile boolean legacyManualLanInjectorStarted;
    private static volatile boolean legacyLanPacketBridgeStarted;
    private static volatile String legacyLanLastLoggedForwardedHost;
    private static int lanBroadcastFailureLogCount;
    private static int legacyLanPacketBridgeFailureLogCount;
    private static final int GLFW_GAMEPAD_STATE_SIZE = 40;
    private static final int GLFW_GAMEPAD_BUTTON_COUNT = 15;
    private static final int GLFW_GAMEPAD_AXIS_COUNT = 6;
    private static final int GLFW_GAMEPAD_AXIS_OFFSET = 16;
    private static final long LEGACY_GAMEPAD_CACHE_NANOS = 5000000L;

    private Lwjgl2XboxAgent() {
    }

    public static void premain(String agentArgs, Instrumentation instrumentation) {
        instrumentation.addTransformer(new Transformer(), false);
        startLanBroadcasterIfEnabled();
        log("compatibility agent installed");
    }

    private static synchronized void log(String message) {
        String line = "[LWJGL2-Xbox] " + message;
        System.err.println(line);

        String userHome = System.getProperty("user.home", ".");
        File logDir = new File(userHome, "logs");
        if (!logDir.exists()) {
            logDir.mkdirs();
        }

        File logFile = new File(logDir, "lwjgl2-xbox-agent.log");
        try (FileWriter writer = new FileWriter(logFile, true)) {
            writer.write(line);
            writer.write(System.lineSeparator());
        } catch (IOException ignored) {
        }
    }

    private static void startLanBroadcasterIfEnabled() {
        if (!Boolean.getBoolean(LAN_PLAY_PROPERTY) || lanBroadcasterStarted) {
            return;
        }

        lanBroadcasterStarted = true;
        Thread thread = new Thread(new LanBroadcaster(), "Xbox LAN advertiser");
        thread.setDaemon(true);
        thread.start();
        log("LAN advertiser started");
    }

    private static final class LanBroadcaster implements Runnable {
        private int advertisedPort;
        private long lastPortLogTime;

        @Override
        public void run() {
            while (true) {
                try {
                    int port = findLanPort();
                    if (port > 0) {
                        if (port != advertisedPort || System.currentTimeMillis() - lastPortLogTime > 30000L) {
                            advertisedPort = port;
                            lastPortLogTime = System.currentTimeMillis();
                            log("LAN advertiser using detected port " + port);
                        }
                        advertise(port);
                    }
                    Thread.sleep(LAN_ADVERTISE_INTERVAL_MS);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    return;
                } catch (Throwable ex) {
                    logLanFailure("advertise loop", ex);
                    sleepQuietly(LAN_ADVERTISE_INTERVAL_MS);
                }
            }
        }

        private int findLanPort() {
            String logPath = System.getProperty(LAN_LOG_PATH_PROPERTY, "");
            if (logPath.length() == 0) {
                return 0;
            }

            File file = new File(logPath);
            if (!file.isFile()) {
                return 0;
            }

            Matcher matcher = LAN_PORT_PATTERN.matcher(readTail(file, 65536));
            int port = 0;
            while (matcher.find()) {
                int candidate = parsePort(matcher.group(3));
                if (candidate > 0) {
                    port = candidate;
                }
            }
            return port;
        }

        private void advertise(int port) throws IOException {
            String motd = System.getProperty(LAN_MOTD_PROPERTY, "Xbox Minecraft");
            String message = "[MOTD]" + motd + "[/MOTD][AD]" + port + "[/AD]";
            byte[] data = message.getBytes(StandardCharsets.UTF_8);
            InetAddress group = InetAddress.getByName(LAN_GROUP_ADDRESS);
            List<Inet4Address> addresses = getLocalIpv4Addresses();

            if (addresses.isEmpty()) {
                sendMulticast(null, group, data);
                sendGlobalBroadcast(data);
                return;
            }

            for (Inet4Address address : addresses) {
                sendMulticast(address, group, data);
                sendBroadcasts(address, data);
            }
        }

        private void sendMulticast(Inet4Address localAddress, InetAddress group, byte[] data) throws IOException {
            MulticastSocket socket = new MulticastSocket();
            try {
                socket.setTimeToLive(1);
                socket.setLoopbackMode(false);
                if (localAddress != null) {
                    socket.setInterface(localAddress);
                }
                socket.send(new DatagramPacket(data, data.length, group, LAN_GROUP_PORT));
            } finally {
                socket.close();
            }
        }

        private void sendBroadcasts(Inet4Address localAddress, byte[] data) {
            try {
                NetworkInterface networkInterface = NetworkInterface.getByInetAddress(localAddress);
                if (networkInterface != null) {
                    for (InterfaceAddress interfaceAddress : networkInterface.getInterfaceAddresses()) {
                        InetAddress broadcast = interfaceAddress.getBroadcast();
                        if (broadcast != null) {
                            sendBroadcast(localAddress, broadcast, data);
                        }
                    }
                }
            } catch (Throwable ex) {
                logLanFailure("subnet broadcast", ex);
            }

            try {
                sendGlobalBroadcast(data);
            } catch (Throwable ex) {
                logLanFailure("global broadcast", ex);
            }
        }

        private void sendBroadcast(Inet4Address localAddress, InetAddress broadcast, byte[] data) throws IOException {
            DatagramSocket socket = new DatagramSocket(0, localAddress);
            try {
                socket.setBroadcast(true);
                socket.send(new DatagramPacket(data, data.length, broadcast, LAN_GROUP_PORT));
            } finally {
                socket.close();
            }
        }

        private void sendGlobalBroadcast(byte[] data) throws IOException {
            DatagramSocket socket = new DatagramSocket();
            try {
                socket.setBroadcast(true);
                socket.send(new DatagramPacket(data, data.length, InetAddress.getByName("255.255.255.255"), LAN_GROUP_PORT));
            } finally {
                socket.close();
            }
        }

        private List<Inet4Address> getLocalIpv4Addresses() {
            List<Inet4Address> addresses = new ArrayList<Inet4Address>();
            try {
                Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
                while (interfaces != null && interfaces.hasMoreElements()) {
                    NetworkInterface networkInterface = interfaces.nextElement();
                    if (!networkInterface.isUp() || networkInterface.isLoopback()) {
                        continue;
                    }
                    Enumeration<InetAddress> inetAddresses = networkInterface.getInetAddresses();
                    while (inetAddresses.hasMoreElements()) {
                        InetAddress address = inetAddresses.nextElement();
                        if (address instanceof Inet4Address && !address.isLoopbackAddress() && !address.isAnyLocalAddress()) {
                            addresses.add((Inet4Address)address);
                        }
                    }
                }
            } catch (Throwable ex) {
                logLanFailure("enumerate IPv4 interfaces", ex);
            }
            return addresses;
        }
    }

    public static synchronized void startLegacyManualLanServerInjector(final Object lanServerList) {
        if (!Boolean.getBoolean(LAN_PLAY_PROPERTY) || lanServerList == null) {
            return;
        }

        Method addServer = findLegacyLanServerAddMethod(lanServerList);
        if (addServer == null) {
            log("legacy LAN support could not find LanServerList.addServer on " + lanServerList.getClass().getName());
            return;
        }

        if (!legacyManualLanInjectorStarted) {
            legacyManualLanInjectorStarted = true;
            Thread thread = new Thread(new Runnable() {
                @Override
                public void run() {
                    legacyManualLanServerLoop(lanServerList, addServer);
                }
            }, "Xbox manual LAN server injector");
            thread.setDaemon(true);
            thread.start();
            log("manual LAN server injector started files=" + manualServerFiles());
        }

        if (!legacyLanPacketBridgeStarted) {
            legacyLanPacketBridgeStarted = true;
            Thread thread = new Thread(new Runnable() {
                @Override
                public void run() {
                    legacyLanPacketBridgeLoop(lanServerList, addServer);
                }
            }, "Xbox LAN packet bridge");
            thread.setDaemon(true);
            thread.start();
            log("LAN packet bridge starting for " + lanServerList.getClass().getName());
        }
    }

    public static void logLegacyDirectConnect(String host, int port) {
        log("legacy direct connect requested host=" + host + " port=" + port);
    }

    private static void legacyManualLanServerLoop(Object lanServerList, Method addServer) {
        Set<String> loggedServers = new HashSet<String>();
        while (true) {
            try {
                List<ManualServer> servers = readManualServers();
                for (ManualServer server : servers) {
                    InetAddress address = InetAddress.getByName(server.host);
                    String message = "[MOTD]" + sanitizeLanTagText(server.name) + "[/MOTD][AD]" + server.port + "[/AD]";
                    addServer.invoke(lanServerList, message, address);

                    String key = server.host + ":" + server.port;
                    if (loggedServers.add(key)) {
                        log("manual LAN server injected " + server.name + " at " + key);
                    }
                }
            } catch (Throwable ex) {
                logLanFailure("manual server inject", ex);
            }

            sleepQuietly(MANUAL_LAN_INJECT_INTERVAL_MS);
        }
    }

    private static void legacyLanPacketBridgeLoop(Object lanServerList, Method addServer) {
        List<DatagramChannel> channels = new ArrayList<DatagramChannel>();
        List<MulticastSocket> sockets = new ArrayList<MulticastSocket>();
        try {
            openLegacyLanReceivers(channels, sockets);
            log("LAN packet bridge receiver started nio=" + channels.size() + " legacy=" + sockets.size());
            ByteBuffer buffer = ByteBuffer.allocate(2048);
            byte[] bytes = new byte[2048];
            while (!channels.isEmpty() || !sockets.isEmpty()) {
                receiveLegacyLanNioPackets(channels, buffer, lanServerList, addServer);
                receiveLegacyLanSocketPackets(sockets, bytes, lanServerList, addServer);
                sleepQuietly(50L);
            }
        } catch (Throwable ex) {
            logLanPacketBridgeFailure("loop", ex);
        } finally {
            for (int i = 0; i < channels.size(); i++) {
                closeQuietly(channels.get(i));
            }
            for (int i = 0; i < sockets.size(); i++) {
                closeQuietly(sockets.get(i));
            }
            log("LAN packet bridge receiver stopped");
        }
    }

    private static void openLegacyLanReceivers(List<DatagramChannel> channels, List<MulticastSocket> sockets) {
        List<Inet4Address> addresses = getUsableLanIpv4Addresses();
        for (int i = 0; i < addresses.size(); i++) {
            Inet4Address address = addresses.get(i);
            NetworkInterface networkInterface = null;
            try {
                networkInterface = NetworkInterface.getByInetAddress(address);
            } catch (Throwable ex) {
                logLanPacketBridgeFailure("interface lookup " + address.getHostAddress(), ex);
            }
            if (networkInterface == null) {
                continue;
            }

            openLegacyLanNioReceiver(channels, networkInterface, address, true);
            openLegacyLanNioReceiver(channels, networkInterface, address, false);
            openLegacyLanSocketReceiver(sockets, networkInterface, address);
        }
    }

    private static void openLegacyLanNioReceiver(
            List<DatagramChannel> channels,
            NetworkInterface networkInterface,
            Inet4Address localAddress,
            boolean wildcardBind) {
        DatagramChannel channel = null;
        try {
            ProtocolFamily family = StandardProtocolFamily.INET;
            channel = DatagramChannel.open(family);
            channel.setOption(StandardSocketOptions.SO_REUSEADDR, Boolean.TRUE);
            channel.setOption(StandardSocketOptions.IP_MULTICAST_IF, networkInterface);
            channel.configureBlocking(false);
            channel.bind(wildcardBind
                    ? new InetSocketAddress(LAN_GROUP_PORT)
                    : new InetSocketAddress(localAddress, LAN_GROUP_PORT));
            channel.join(InetAddress.getByName(LAN_GROUP_ADDRESS), networkInterface);
            channels.add(channel);
            log("LAN packet bridge NIO joined bind="
                    + (wildcardBind ? "0.0.0.0" : localAddress.getHostAddress())
                    + " interface=" + describeNetworkInterface(networkInterface));
        } catch (Throwable ex) {
            closeQuietly(channel);
            logLanPacketBridgeFailure(
                    "NIO join bind=" + (wildcardBind ? "0.0.0.0" : localAddress.getHostAddress())
                            + " interface=" + describeNetworkInterface(networkInterface),
                    ex);
        }
    }

    private static void openLegacyLanSocketReceiver(
            List<MulticastSocket> sockets,
            NetworkInterface networkInterface,
            Inet4Address localAddress) {
        MulticastSocket socket = null;
        try {
            socket = new MulticastSocket(null);
            socket.setReuseAddress(true);
            socket.bind(new InetSocketAddress(LAN_GROUP_PORT));
            socket.setSoTimeout(50);
            socket.setInterface(localAddress);
            socket.joinGroup(InetAddress.getByName(LAN_GROUP_ADDRESS));
            sockets.add(socket);
            log("LAN packet bridge legacy joined address="
                    + localAddress.getHostAddress()
                    + " interface=" + describeNetworkInterface(networkInterface));
        } catch (Throwable ex) {
            closeQuietly(socket);
            logLanPacketBridgeFailure(
                    "legacy join address=" + localAddress.getHostAddress()
                            + " interface=" + describeNetworkInterface(networkInterface),
                    ex);
        }
    }

    private static void receiveLegacyLanNioPackets(
            List<DatagramChannel> channels,
            ByteBuffer buffer,
            Object lanServerList,
            Method addServer) {
        for (int i = 0; i < channels.size(); i++) {
            DatagramChannel channel = channels.get(i);
            try {
                buffer.clear();
                SocketAddress sender = channel.receive(buffer);
                if (sender instanceof InetSocketAddress) {
                    InetSocketAddress inetSender = (InetSocketAddress)sender;
                    buffer.flip();
                    String packetText = StandardCharsets.UTF_8.decode(buffer).toString();
                    injectLegacyLanPacket("NIO", packetText, inetSender.getAddress(), lanServerList, addServer);
                }
            } catch (Throwable ex) {
                logLanPacketBridgeFailure("NIO receive", ex);
            }
        }
    }

    private static void receiveLegacyLanSocketPackets(
            List<MulticastSocket> sockets,
            byte[] bytes,
            Object lanServerList,
            Method addServer) {
        for (int i = 0; i < sockets.size(); i++) {
            MulticastSocket socket = sockets.get(i);
            try {
                DatagramPacket packet = new DatagramPacket(bytes, bytes.length);
                socket.receive(packet);
                String packetText = new String(
                        packet.getData(),
                        packet.getOffset(),
                        packet.getLength(),
                        StandardCharsets.UTF_8);
                injectLegacyLanPacket("legacy", packetText, packet.getAddress(), lanServerList, addServer);
            } catch (java.net.SocketTimeoutException ignored) {
            } catch (Throwable ex) {
                logLanPacketBridgeFailure("legacy receive", ex);
            }
        }
    }

    private static void injectLegacyLanPacket(
            String source,
            String packetText,
            InetAddress sender,
            Object lanServerList,
            Method addServer) {
        if (sender == null || packetText == null || packetText.indexOf("[AD]") < 0) {
            return;
        }

        try {
            ForwardedLanPacket packet = resolveForwardedLanPacket(packetText, sender);
            addServer.invoke(lanServerList, packet.message, packet.sender);
            log("LAN packet bridge " + source + " packet injected from "
                    + packet.sender.getHostAddress() + " text=" + packet.message);
        } catch (Throwable ex) {
            logLanPacketBridgeFailure("packet inject from " + sender.getHostAddress(), ex);
        }
    }

    private static ForwardedLanPacket resolveForwardedLanPacket(String packetText, InetAddress sender) {
        int start = packetText.indexOf(FORWARDED_HOST_START);
        if (start < 0) {
            return new ForwardedLanPacket(packetText, sender);
        }

        int hostStart = start + FORWARDED_HOST_START.length();
        int end = packetText.indexOf(FORWARDED_HOST_END, hostStart);
        if (end <= hostStart) {
            return new ForwardedLanPacket(packetText, sender);
        }

        try {
            String host = packetText.substring(hostStart, end).trim();
            InetAddress forwarded = InetAddress.getByName(host);
            if (forwarded instanceof Inet4Address
                    && !forwarded.isLoopbackAddress()
                    && !forwarded.isAnyLocalAddress()) {
                String cleanPacket = packetText.substring(0, start)
                        + packetText.substring(end + FORWARDED_HOST_END.length());
                if (!host.equals(legacyLanLastLoggedForwardedHost)) {
                    legacyLanLastLoggedForwardedHost = host;
                    log("LAN packet bridge using forwarded PC host " + host);
                }
                return new ForwardedLanPacket(cleanPacket, forwarded);
            }
        } catch (Throwable ex) {
            logLanPacketBridgeFailure("forwarded host parse", ex);
        }

        return new ForwardedLanPacket(packetText, sender);
    }

    private static Method findLegacyLanServerAddMethod(Object lanServerList) {
        try {
            Method method = lanServerList.getClass().getMethod("a", String.class, InetAddress.class);
            method.setAccessible(true);
            return method;
        } catch (Throwable ignored) {
        }

        Method[] methods = lanServerList.getClass().getMethods();
        for (int i = 0; i < methods.length; i++) {
            Method method = methods[i];
            Class<?>[] parameters = method.getParameterTypes();
            if (parameters.length == 2
                    && String.class.equals(parameters[0])
                    && InetAddress.class.equals(parameters[1])
                    && Void.TYPE.equals(method.getReturnType())) {
                method.setAccessible(true);
                return method;
            }
        }

        return null;
    }

    private static List<Inet4Address> getUsableLanIpv4Addresses() {
        List<Inet4Address> addresses = new ArrayList<Inet4Address>();
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface networkInterface = interfaces.nextElement();
                if (!networkInterface.isUp() || networkInterface.isLoopback()) {
                    continue;
                }

                Enumeration<InetAddress> inetAddresses = networkInterface.getInetAddresses();
                while (inetAddresses.hasMoreElements()) {
                    InetAddress address = inetAddresses.nextElement();
                    if (address instanceof Inet4Address
                            && !address.isLoopbackAddress()
                            && !address.isAnyLocalAddress()) {
                        addresses.add((Inet4Address)address);
                    }
                }
            }
        } catch (Throwable ex) {
            logLanPacketBridgeFailure("enumerate IPv4 interfaces", ex);
        }
        return addresses;
    }

    private static String describeNetworkInterface(NetworkInterface networkInterface) {
        if (networkInterface == null) {
            return "null";
        }
        try {
            return networkInterface.getName() + "(" + networkInterface.getDisplayName() + ")";
        } catch (Throwable ignored) {
            return String.valueOf(networkInterface);
        }
    }

    private static void closeQuietly(DatagramChannel channel) {
        if (channel == null) {
            return;
        }
        try {
            channel.close();
        } catch (Throwable ignored) {
        }
    }

    private static void closeQuietly(MulticastSocket socket) {
        if (socket == null) {
            return;
        }
        try {
            socket.close();
        } catch (Throwable ignored) {
        }
    }

    private static synchronized void logLanPacketBridgeFailure(String action, Throwable ex) {
        if (legacyLanPacketBridgeFailureLogCount < 40) {
            legacyLanPacketBridgeFailureLogCount++;
            log("LAN packet bridge " + action + " failed: " + ex);
        }
    }

    private static List<ManualServer> readManualServers() {
        List<ManualServer> servers = new ArrayList<ManualServer>();
        Set<String> seen = new HashSet<String>();

        List<File> files = manualServerFiles();
        for (int i = 0; i < files.size(); i++) {
            File file = files.get(i);
            if (file == null || !file.isFile()) {
                continue;
            }

            try {
                List<String> lines = Files.readAllLines(file.toPath(), StandardCharsets.UTF_8);
                for (int j = 0; j < lines.size(); j++) {
                    ManualServer server = parseManualServer(lines.get(j));
                    if (server == null) {
                        continue;
                    }

                    String key = server.host + ":" + server.port;
                    if (seen.add(key)) {
                        servers.add(server);
                    }
                }
            } catch (Throwable ex) {
                logLanFailure("manual server file read " + file, ex);
            }
        }

        return servers;
    }

    private static List<File> manualServerFiles() {
        List<File> files = new ArrayList<File>();
        String configured = System.getProperty(LAN_MANUAL_SERVERS_PROPERTY, "").trim();
        if (configured.length() > 0) {
            String[] paths = configured.split(";");
            for (int i = 0; i < paths.length; i++) {
                String path = paths[i].trim();
                if (path.length() > 0) {
                    files.add(new File(path));
                }
            }
        }

        File gameDir = new File(System.getProperty("user.home", "."));
        files.add(new File(gameDir, "xbox-lan-servers.txt"));
        files.add(new File(gameDir, "lan-servers.txt"));
        files.add(new File(new File(gameDir, "config"), "xbox-lan-servers.txt"));

        File profileRoot = gameDir.getParentFile();
        File localState = profileRoot == null ? null : profileRoot.getParentFile();
        if (localState != null) {
            files.add(new File(localState, "xbox-lan-servers.txt"));
            files.add(new File(localState, "lan-servers.txt"));
        }

        return files;
    }

    private static ManualServer parseManualServer(String line) {
        if (line == null) {
            return null;
        }

        String text = line.trim();
        if (text.length() == 0 || text.startsWith("#") || text.startsWith("//")) {
            return null;
        }

        String name = "PC LAN World";
        int equals = text.indexOf('=');
        if (equals >= 0) {
            name = text.substring(0, equals).trim();
            text = text.substring(equals + 1).trim();
        }

        int comment = text.indexOf('#');
        if (comment >= 0) {
            text = text.substring(0, comment).trim();
        }
        if (text.startsWith("minecraft://")) {
            text = text.substring("minecraft://".length());
        }

        int colon = text.lastIndexOf(':');
        if (colon <= 0 || colon >= text.length() - 1) {
            return null;
        }

        String host = text.substring(0, colon).trim();
        String portText = text.substring(colon + 1).trim();
        try {
            int port = Integer.parseInt(portText);
            if (host.length() == 0 || port < 1 || port > 65535) {
                return null;
            }
            if (name.length() == 0) {
                name = "PC LAN World";
            }
            return new ManualServer(name, host, port);
        } catch (NumberFormatException ignored) {
            return null;
        }
    }

    private static String sanitizeLanTagText(String text) {
        if (text == null || text.trim().length() == 0) {
            return "PC LAN World";
        }

        String sanitized = text.replace('[', '(').replace(']', ')').trim();
        return sanitized.length() > 64 ? sanitized.substring(0, 64) : sanitized;
    }

    private static final class ManualServer {
        final String name;
        final String host;
        final int port;

        ManualServer(String name, String host, int port) {
            this.name = name;
            this.host = host;
            this.port = port;
        }
    }

    private static final class ForwardedLanPacket {
        final String message;
        final InetAddress sender;

        ForwardedLanPacket(String message, InetAddress sender) {
            this.message = message;
            this.sender = sender;
        }
    }

    private static String readTail(File file, int maxBytes) {
        RandomAccessFile raf = null;
        try {
            raf = new RandomAccessFile(file, "r");
            long length = raf.length();
            long start = Math.max(0L, length - maxBytes);
            raf.seek(start);
            byte[] bytes = new byte[(int)(length - start)];
            raf.readFully(bytes);
            return new String(bytes, StandardCharsets.UTF_8);
        } catch (Throwable ignored) {
            return "";
        } finally {
            if (raf != null) {
                try {
                    raf.close();
                } catch (IOException ignored) {
                }
            }
        }
    }

    private static int parsePort(String value) {
        try {
            int port = Integer.parseInt(value);
            return port >= 1024 && port <= 65535 ? port : 0;
        } catch (Throwable ignored) {
            return 0;
        }
    }

    private static void sleepQuietly(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
    }

    private static synchronized void logLanFailure(String action, Throwable ex) {
        if (lanBroadcastFailureLogCount < 20) {
            lanBroadcastFailureLogCount++;
            log("LAN advertiser " + action + " failed: " + ex);
        }
    }

    public static void presentLegacyFrame() {
        try {
            Object function = wglSwapBuffersFunction;
            if (function == null && !wglSwapLookupAttempted) {
                synchronized (Lwjgl2XboxAgent.class) {
                    function = wglSwapBuffersFunction;
                    if (function == null && !wglSwapLookupAttempted) {
                        wglSwapLookupAttempted = true;
                        Class<?> nativeLibraryClass = Class.forName("com.sun.jna.NativeLibrary");
                        Object library = nativeLibraryClass.getMethod("getInstance", String.class).invoke(null, "opengl32");
                        function = nativeLibraryClass.getMethod("getFunction", String.class).invoke(library, "wglSwapBuffers");
                        wglSwapBuffersFunction = function;
                        log("JNA wglSwapBuffers bridge ready");
                    }
                }
            }

            if (function != null) {
                Class<?> pointerClass = Class.forName("com.sun.jna.Pointer");
                Object nullPointer = pointerClass.getField("NULL").get(null);
                function.getClass().getMethod("invokeInt", Object[].class).invoke(function, new Object[] { new Object[] { nullPointer } });
            }
        } catch (Throwable ex) {
            if (!wglSwapLookupAttempted) {
                log("wglSwapBuffers bridge failed: " + ex);
            }
            wglSwapLookupAttempted = true;
        }
    }

    public static void createLegacyKeyboard() {
        if (legacyKeyboardLookupFailed) {
            return;
        }

        try {
            Object function = getLegacyKeyboardFunction("xglLegacyKeyboardCreate");
            legacyKeyboardCreated = invokeLegacyKeyboardInt(function) != 0;
            if (legacyKeyboardCreated && !legacyKeyboardReadyLogged) {
                legacyKeyboardReadyLogged = true;
                log("LWJGL2 keyboard bridge ready");
            }
        } catch (Throwable ex) {
            legacyKeyboardLookupFailed = true;
            log("LWJGL2 keyboard bridge unavailable: " + ex);
        }
    }

    public static void destroyLegacyKeyboard() {
        if (legacyKeyboardLookupFailed) {
            return;
        }

        try {
            Object function = getLegacyKeyboardFunction("xglLegacyKeyboardDestroy");
            function.getClass().getMethod("invokeVoid", Object[].class).invoke(function, new Object[] { new Object[0] });
        } catch (Throwable ignored) {
        }
        legacyKeyboardCreated = false;
    }

    public static void pollLegacyKeyboard(ByteBuffer keyDownBuffer) {
        if (keyDownBuffer == null) {
            return;
        }

        createLegacyKeyboard();
        if (!legacyKeyboardCreated) {
            return;
        }

        try {
            Object function = getLegacyKeyboardFunction("xglLegacyKeyboardGetKeyState");
            int keyCount = Math.min(LEGACY_KEY_COUNT, keyDownBuffer.limit());
            for (int key = 0; key < keyCount; key++) {
                int down = invokeLegacyKeyboardInt(function, Integer.valueOf(key));
                keyDownBuffer.put(key, (byte)(down != 0 ? 1 : 0));
            }
        } catch (Throwable ex) {
            logLegacyKeyboardFailure("poll", ex);
        }
    }

    public static void readLegacyKeyboard(ByteBuffer eventBuffer) {
        if (eventBuffer == null) {
            return;
        }

        createLegacyKeyboard();
        if (!legacyKeyboardCreated) {
            return;
        }

        try {
            Object pollEvent = getLegacyKeyboardFunction("xglLegacyKeyboardPollEvent");
            Object getEventKey = getLegacyKeyboardFunction("xglLegacyKeyboardGetEventKey");
            Object getEventState = getLegacyKeyboardFunction("xglLegacyKeyboardGetEventState");
            Object getEventChar = getLegacyKeyboardFunction("xglLegacyKeyboardGetEventChar");
            Object getEventRepeat = getLegacyKeyboardFunction("xglLegacyKeyboardGetEventRepeat");

            while (eventBuffer.remaining() >= LEGACY_KEYBOARD_EVENT_SIZE && invokeLegacyKeyboardInt(pollEvent) != 0) {
                int key = invokeLegacyKeyboardInt(getEventKey) & 255;
                int state = invokeLegacyKeyboardInt(getEventState);
                int ch = invokeLegacyKeyboardInt(getEventChar);
                int repeat = invokeLegacyKeyboardInt(getEventRepeat);

                eventBuffer.putInt(key);
                eventBuffer.put((byte)(state != 0 ? 1 : 0));
                eventBuffer.putInt(ch);
                eventBuffer.putLong(System.nanoTime());
                eventBuffer.put((byte)(repeat != 0 ? 1 : 0));
            }
        } catch (Throwable ex) {
            logLegacyKeyboardFailure("read", ex);
        }
    }

    public static long resolveOpenGlFunctionAddress(String name) {
        if (name == null || name.length() == 0) {
            return 0L;
        }

        long address = 0L;
        try {
            Object library = getOpenGlLibrary();
            if (library != null) {
                address = getFunctionPointer(library, name);
                if (address == 0L) {
                    address = callWglGetProcAddress(library, name);
                }
            }
        } catch (Throwable ignored) {
        }

        if (shouldLogOpenGlResolve(name)) {
            synchronized (Lwjgl2XboxAgent.class) {
                if (openGlResolveLogCount < 160) {
                    openGlResolveLogCount++;
                    log("GLContext.getFunctionAddress " + name + " -> 0x" + Long.toHexString(address));
                }
            }
        }

        return address;
    }

    private static Object getOpenGlLibrary() throws Exception {
        Object library = openGlLibrary;
        if (library != null) {
            return library;
        }

        synchronized (Lwjgl2XboxAgent.class) {
            library = openGlLibrary;
            if (library == null) {
                String libraryName = System.getProperty("org.lwjgl.opengl.libname");
                if (libraryName == null || libraryName.length() == 0) {
                    libraryName = "opengl32";
                }
                Class<?> nativeLibraryClass = Class.forName("com.sun.jna.NativeLibrary");
                library = nativeLibraryClass.getMethod("getInstance", String.class).invoke(null, libraryName);
                openGlLibrary = library;
                log("GLContext resolver using " + libraryName);
            }
            return library;
        }
    }

    private static Object getLegacyKeyboardFunction(String name) throws Exception {
        synchronized (Lwjgl2XboxAgent.class) {
            Object function = legacyKeyboardFunctions.get(name);
            if (function == null) {
                Object library = getOpenGlLibrary();
                Class<?> nativeLibraryClass = Class.forName("com.sun.jna.NativeLibrary");
                function = nativeLibraryClass.getMethod("getFunction", String.class).invoke(library, name);
                legacyKeyboardFunctions.put(name, function);
            }
            return function;
        }
    }

    private static int invokeLegacyKeyboardInt(Object function, Object... args) throws Exception {
        Object value = function.getClass().getMethod("invokeInt", Object[].class).invoke(function, new Object[] { args });
        return ((Number)value).intValue();
    }

    private static void logLegacyKeyboardFailure(String operation, Throwable ex) {
        synchronized (Lwjgl2XboxAgent.class) {
            if (legacyKeyboardFailureLogCount < 8) {
                legacyKeyboardFailureLogCount++;
                log("LWJGL2 keyboard " + operation + " failed: " + ex);
            }
        }
    }

    public static boolean isLegacyControllableControllerPresent() {
        return refreshLegacyGamepadState();
    }

    public static Object getLegacyControllableController(Object manager) {
        if (manager == null || !isLegacyControllableControllerPresent()) {
            return null;
        }

        Object controller = legacyControllableController;
        if (controller != null && manager == legacyControllableControllerManager) {
            return controller;
        }

        synchronized (Lwjgl2XboxAgent.class) {
            controller = legacyControllableController;
            if (controller != null && manager == legacyControllableControllerManager) {
                return controller;
            }

            try {
                ClassLoader loader = manager.getClass().getClassLoader();
                Class<?> controllerClass = Class.forName("uk.co.electronstudio.sdl2gdx.SDL2Controller", true, loader);
                controller = controllerClass
                        .getConstructor(manager.getClass(), Integer.TYPE)
                        .newInstance(manager, Integer.valueOf(0));
                legacyControllableControllerManager = manager;
                legacyControllableController = controller;
                log("Controllable synthetic Xbox controller ready");
                return controller;
            } catch (Throwable ex) {
                logLegacyControllableFailure("create synthetic controller", ex);
                return null;
            }
        }
    }

    public static void updateLegacyControllableControllers(Map controllers, Set listeners) {
        if (controllers == null) {
            return;
        }

        Integer id = Integer.valueOf(0);
        boolean present = isLegacyControllableControllerPresent();
        boolean wasPresent = controllers.containsKey(id);
        if (present) {
            controllers.put(id, "Xbox Gamepad");
            if (!wasPresent) {
                notifyLegacyControllableListeners(listeners, "connected", 0);
            }
        } else if (wasPresent) {
            controllers.remove(id);
            notifyLegacyControllableListeners(listeners, "disconnected", 0);
        }
    }

    public static Map getLegacyControllableControllers(Map controllers) {
        updateLegacyControllableControllers(controllers, null);
        return controllers;
    }

    public static String getLegacyControllableControllerName(Map controllers, int id) {
        updateLegacyControllableControllers(controllers, null);
        Object name = controllers == null ? null : controllers.get(Integer.valueOf(id));
        return name instanceof String ? (String)name : null;
    }

    public static int getLegacyControllableControllerCount(Map controllers) {
        updateLegacyControllableControllers(controllers, null);
        return controllers == null ? 0 : controllers.size();
    }

    public static int getLegacyControllableFirstControllerJid() {
        return isLegacyControllableControllerPresent() ? 0 : -1;
    }

    public static boolean legacyControllableButton(int button) {
        int glfwButton = mapSdlButtonToGlfw(button);
        if (glfwButton < 0 || glfwButton >= GLFW_GAMEPAD_BUTTON_COUNT || !refreshLegacyGamepadState()) {
            return false;
        }
        updateLegacyControllablePointerModeToggle();
        synchronized (legacyGamepadLock) {
            return legacyGamepadButtons[glfwButton] != 0;
        }
    }

    public static float legacyControllableAxis(int axis) {
        if (axis < 0 || axis >= GLFW_GAMEPAD_AXIS_COUNT || !refreshLegacyGamepadState()) {
            return 0.0f;
        }
        updateLegacyControllablePointerModeToggle();
        synchronized (legacyGamepadLock) {
            return legacyGamepadAxes[axis];
        }
    }

    public static boolean isLegacyControllablePointerModeActive() {
        return updateLegacyControllablePointerModeToggle();
    }

    public static boolean legacyControllableShouldTreatMouseGrabbed(boolean grabbed) {
        return grabbed && !isLegacyControllablePointerModeActive();
    }

    public static void tickLegacyControllablePointer(Object controllerInput) {
        if (controllerInput == null || !updateLegacyControllablePointerModeToggle()) {
            return;
        }

        try {
            ClassLoader loader = controllerInput.getClass().getClassLoader();
            Object minecraft = getLegacyMinecraft(loader);
            if (minecraft == null) {
                return;
            }

            int width = getIntField(minecraft, "field_71443_c", 1920);
            int height = getIntField(minecraft, "field_71440_d", 1080);
            if (width <= 0) width = 1920;
            if (height <= 0) height = 1080;

            int previousX = getIntField(controllerInput, "targetMouseX", width / 2);
            int previousY = getIntField(controllerInput, "targetMouseY", height / 2);
            if (previousX <= 0 && previousY <= 0) {
                previousX = width / 2;
                previousY = height / 2;
            }

            float leftX;
            float leftY;
            float rightX;
            float rightY;
            synchronized (legacyGamepadLock) {
                leftX = legacyGamepadAxes[0];
                leftY = legacyGamepadAxes[1];
                rightX = legacyGamepadAxes[2];
                rightY = legacyGamepadAxes[3];
            }

            double xAxis = applyLegacyPointerDeadzone(rightX);
            double yAxis = applyLegacyPointerDeadzone(-rightY);
            if (Math.abs(xAxis) < 0.01 && Math.abs(yAxis) < 0.01) {
                xAxis = applyLegacyPointerDeadzone(leftX);
                yAxis = applyLegacyPointerDeadzone(leftY);
            }

            double speed = Math.max(8.0, Math.min(42.0, Math.max(width, height) / 150.0));
            int currentX = clampLegacyPointerInt((int)Math.round(previousX + (xAxis * speed)), 0, width - 1);
            int currentY = clampLegacyPointerInt((int)Math.round(previousY + (yAxis * speed)), 0, height - 1);

            setIntField(controllerInput, "prevTargetMouseX", previousX);
            setIntField(controllerInput, "prevTargetMouseY", previousY);
            setIntField(controllerInput, "targetMouseX", currentX);
            setIntField(controllerInput, "targetMouseY", currentY);
            setDoubleField(controllerInput, "virtualMouseX", currentX);
            setDoubleField(controllerInput, "virtualMouseY", currentY);
            setDoubleField(controllerInput, "mouseSpeedX", 0.0);
            setDoubleField(controllerInput, "mouseSpeedY", 0.0);
            setBooleanField(controllerInput, "moving", currentX != previousX || currentY != previousY);
            setIntField(controllerInput, "lastUse", 100);
        } catch (Throwable ex) {
            logLegacyControllablePointerFailure("tick pointer", ex);
        }
    }

    public static void renderLegacyControllablePointerOverlay(Object event) {
        if (event == null || !isLegacyControllablePointerModeActive()) {
            return;
        }

        try {
            Object type = event.getClass().getMethod("getType").invoke(event);
            if (type instanceof Enum && !"ALL".equals(((Enum)type).name())) {
                return;
            }

            ClassLoader loader = event.getClass().getClassLoader();
            Class<?> controllableClass = Class.forName("com.mrcrayfish.controllable.Controllable", true, loader);
            Object input = controllableClass.getMethod("getInput").invoke(null);
            if (input == null) {
                return;
            }
            tickLegacyControllablePointer(input);

            Object minecraft = getLegacyMinecraft(loader);
            if (minecraft == null) {
                return;
            }

            int displayWidth = getIntField(minecraft, "field_71443_c", 1920);
            int displayHeight = getIntField(minecraft, "field_71440_d", 1080);
            if (displayWidth <= 0) displayWidth = 1920;
            if (displayHeight <= 0) displayHeight = 1080;

            double virtualX = getDoubleField(input, "virtualMouseX", displayWidth / 2.0);
            double virtualY = getDoubleField(input, "virtualMouseY", displayHeight / 2.0);

            Class<?> minecraftClass = minecraft.getClass();
            Class<?> scaledResolutionClass = Class.forName("net.minecraft.client.gui.ScaledResolution", true, loader);
            Object scaledResolution = scaledResolutionClass.getConstructor(minecraftClass).newInstance(minecraft);
            int scaledWidth = ((Number)scaledResolutionClass.getMethod("func_78326_a").invoke(scaledResolution)).intValue();
            int scaledHeight = ((Number)scaledResolutionClass.getMethod("func_78328_b").invoke(scaledResolution)).intValue();
            if (scaledWidth <= 0 || scaledHeight <= 0) {
                return;
            }

            int guiX = clampLegacyPointerInt((int)Math.round(virtualX * scaledWidth / displayWidth), 0, scaledWidth - 1);
            int guiY = clampLegacyPointerInt((int)Math.round(virtualY * scaledHeight / displayHeight), 0, scaledHeight - 1);
            int cursorOrdinal = getLegacyControllableCursorOrdinal(controllableClass);
            int cursorCount = getLegacyControllableCursorCount(loader);

            Class<?> resourceLocationClass = Class.forName("net.minecraft.util.ResourceLocation", true, loader);
            Object cursorTexture = resourceLocationClass
                    .getConstructor(String.class, String.class)
                    .newInstance("controllable", "textures/gui/cursor.png");
            Object textureManager = minecraftClass.getMethod("func_110434_K").invoke(minecraft);
            textureManager.getClass().getMethod("func_110577_a", resourceLocationClass).invoke(textureManager, cursorTexture);

            Class<?> glStateManagerClass = Class.forName("net.minecraft.client.renderer.GlStateManager", true, loader);
            Class<?> guiScreenClass = Class.forName("net.minecraft.client.gui.GuiScreen", true, loader);
            glStateManagerClass.getMethod("func_179094_E").invoke(null);
            glStateManagerClass.getMethod("func_179147_l").invoke(null);
            glStateManagerClass.getMethod("func_179098_w").invoke(null);
            glStateManagerClass.getMethod("func_179131_c", Float.TYPE, Float.TYPE, Float.TYPE, Float.TYPE)
                    .invoke(null, Float.valueOf(1.0f), Float.valueOf(1.0f), Float.valueOf(1.0f), Float.valueOf(1.0f));
            guiScreenClass
                    .getMethod("func_146110_a", Integer.TYPE, Integer.TYPE, Float.TYPE, Float.TYPE, Integer.TYPE, Integer.TYPE, Float.TYPE, Float.TYPE)
                    .invoke(
                            null,
                            Integer.valueOf(guiX - 8),
                            Integer.valueOf(guiY - 8),
                            Float.valueOf(0.0f),
                            Float.valueOf(cursorOrdinal * 16.0f),
                            Integer.valueOf(16),
                            Integer.valueOf(16),
                            Float.valueOf(32.0f),
                            Float.valueOf(Math.max(1, cursorCount) * 16.0f));
            glStateManagerClass.getMethod("func_179084_k").invoke(null);
            glStateManagerClass.getMethod("func_179121_F").invoke(null);
        } catch (Throwable ex) {
            logLegacyControllablePointerFailure("render pointer", ex);
        }
    }

    private static boolean updateLegacyControllablePointerModeToggle() {
        if (!refreshLegacyGamepadState()) {
            if (legacyControllablePointerToggleLatch) {
                legacyControllablePointerToggleLatch = false;
            }
            legacyControllablePointerMode = false;
            return false;
        }

        boolean chord;
        synchronized (legacyGamepadLock) {
            chord = legacyGamepadButtons[8] != 0 && legacyGamepadButtons[9] != 0;
        }

        if (chord && !legacyControllablePointerToggleLatch) {
            legacyControllablePointerMode = !legacyControllablePointerMode;
            log("Controllable pointer mode " + (legacyControllablePointerMode ? "enabled" : "disabled") + " (L3+R3)");
        }
        legacyControllablePointerToggleLatch = chord;
        return legacyControllablePointerMode;
    }

    private static double applyLegacyPointerDeadzone(double value) {
        double deadzone = 0.16;
        double magnitude = Math.abs(value);
        if (magnitude <= deadzone) {
            return 0.0;
        }

        double scaled = (magnitude - deadzone) / (1.0 - deadzone);
        return value < 0.0 ? -scaled : scaled;
    }

    private static int clampLegacyPointerInt(int value, int low, int high) {
        if (value < low) return low;
        if (value > high) return high;
        return value;
    }

    private static Object getLegacyMinecraft(ClassLoader loader) throws Exception {
        Class<?> minecraftClass = Class.forName("net.minecraft.client.Minecraft", true, loader);
        return minecraftClass.getMethod("func_71410_x").invoke(null);
    }

    private static int getLegacyControllableCursorOrdinal(Class<?> controllableClass) {
        try {
            Object options = controllableClass.getMethod("getOptions").invoke(null);
            Object cursorType = options.getClass().getMethod("getCursorType").invoke(options);
            return cursorType instanceof Enum ? ((Enum)cursorType).ordinal() : 0;
        } catch (Throwable ignored) {
            return 0;
        }
    }

    private static int getLegacyControllableCursorCount(ClassLoader loader) {
        try {
            Class<?> cursorTypeClass = Class.forName("com.mrcrayfish.controllable.client.CursorType", true, loader);
            Object values = cursorTypeClass.getMethod("values").invoke(null);
            return java.lang.reflect.Array.getLength(values);
        } catch (Throwable ignored) {
            return 3;
        }
    }

    private static int getIntField(Object target, String name, int fallback) throws Exception {
        Field field = target.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return ((Number)field.get(target)).intValue();
    }

    private static double getDoubleField(Object target, String name, double fallback) throws Exception {
        Field field = target.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return ((Number)field.get(target)).doubleValue();
    }

    private static void setIntField(Object target, String name, int value) throws Exception {
        Field field = target.getClass().getDeclaredField(name);
        field.setAccessible(true);
        field.setInt(target, value);
    }

    private static void setDoubleField(Object target, String name, double value) throws Exception {
        Field field = target.getClass().getDeclaredField(name);
        field.setAccessible(true);
        field.setDouble(target, value);
    }

    private static void setBooleanField(Object target, String name, boolean value) throws Exception {
        Field field = target.getClass().getDeclaredField(name);
        field.setAccessible(true);
        field.setBoolean(target, value);
    }

    private static void notifyLegacyControllableListeners(Set listeners, String methodName, int id) {
        if (listeners == null) {
            return;
        }

        try {
            for (Object listener : listeners) {
                if (listener != null) {
                    listener.getClass().getMethod(methodName, Integer.TYPE).invoke(listener, Integer.valueOf(id));
                }
            }
        } catch (Throwable ex) {
            logLegacyControllableFailure("notify " + methodName, ex);
        }
    }

    private static boolean refreshLegacyGamepadState() {
        long now = System.nanoTime();
        synchronized (legacyGamepadLock) {
            if (now - legacyGamepadLastReadNanos < LEGACY_GAMEPAD_CACHE_NANOS) {
                return legacyGamepadPresent;
            }
            legacyGamepadLastReadNanos = now;

            try {
                int present = invokeGlfwInt(getGlfwFunction("glfwJoystickPresent"), Integer.valueOf(0));
                if (present == 0) {
                    legacyGamepadPresent = false;
                    return false;
                }

                Class<?> memoryClass = Class.forName("com.sun.jna.Memory");
                Object state = memoryClass.getConstructor(Long.TYPE).newInstance(Long.valueOf(GLFW_GAMEPAD_STATE_SIZE));
                int ok = invokeGlfwInt(getGlfwFunction("glfwGetGamepadState"), Integer.valueOf(0), state);
                if (ok == 0) {
                    legacyGamepadPresent = false;
                    return false;
                }

                for (int i = 0; i < GLFW_GAMEPAD_BUTTON_COUNT; i++) {
                    Object value = memoryClass.getMethod("getByte", Long.TYPE).invoke(state, Long.valueOf(i));
                    legacyGamepadButtons[i] = ((Number)value).byteValue();
                }
                for (int i = 0; i < GLFW_GAMEPAD_AXIS_COUNT; i++) {
                    Object value = memoryClass.getMethod("getFloat", Long.TYPE).invoke(state, Long.valueOf(GLFW_GAMEPAD_AXIS_OFFSET + (i * 4L)));
                    legacyGamepadAxes[i] = ((Number)value).floatValue();
                }

                legacyGamepadPresent = true;
                return true;
            } catch (Throwable ex) {
                legacyGamepadPresent = false;
                logLegacyControllableFailure("read gamepad", ex);
                return false;
            }
        }
    }

    private static Object getGlfwFunction(String name) throws Exception {
        synchronized (Lwjgl2XboxAgent.class) {
            Object function = glfwFunctions.get(name);
            if (function == null) {
                Object library = getGlfwLibrary();
                Class<?> nativeLibraryClass = Class.forName("com.sun.jna.NativeLibrary");
                function = nativeLibraryClass.getMethod("getFunction", String.class).invoke(library, name);
                glfwFunctions.put(name, function);
            }
            return function;
        }
    }

    private static Object getGlfwLibrary() throws Exception {
        Object library = glfwLibrary;
        if (library != null) {
            return library;
        }
        if (glfwLookupFailed) {
            throw new IllegalStateException("xbox-glfw lookup already failed");
        }

        synchronized (Lwjgl2XboxAgent.class) {
            library = glfwLibrary;
            if (library != null) {
                return library;
            }

            String libraryName = System.getProperty("org.lwjgl.glfw.libname");
            if (libraryName == null || libraryName.length() == 0) {
                libraryName = "xbox-glfw";
            }
            try {
                Class<?> nativeLibraryClass = Class.forName("com.sun.jna.NativeLibrary");
                library = nativeLibraryClass.getMethod("getInstance", String.class).invoke(null, libraryName);
                glfwLibrary = library;
                log("Controllable synthetic controller using " + libraryName);
                return library;
            } catch (Exception ex) {
                glfwLookupFailed = true;
                throw ex;
            }
        }
    }

    private static int invokeGlfwInt(Object function, Object... args) throws Exception {
        Object value = function.getClass().getMethod("invokeInt", Object[].class).invoke(function, new Object[] { args });
        return ((Number)value).intValue();
    }

    private static int mapSdlButtonToGlfw(int button) {
        switch (button) {
            case 0: return 0;  // A
            case 1: return 1;  // B
            case 2: return 2;  // X
            case 3: return 3;  // Y
            case 4: return 6;  // Back/View
            case 6: return 7;  // Start/Menu
            case 7: return 8;  // Left stick
            case 8: return 9;  // Right stick
            case 9: return 4;  // Left bumper
            case 10: return 5; // Right bumper
            case 11: return 10; // D-pad up
            case 12: return 12; // D-pad down
            case 13: return 13; // D-pad left
            case 14: return 11; // D-pad right
            default: return -1;
        }
    }

    private static void logLegacyControllableFailure(String operation, Throwable ex) {
        synchronized (Lwjgl2XboxAgent.class) {
            if (legacyControllableFailureLogCount < 12) {
                legacyControllableFailureLogCount++;
                log("Controllable synthetic controller " + operation + " failed: " + ex);
            }
        }
    }

    private static void logLegacyControllablePointerFailure(String operation, Throwable ex) {
        synchronized (Lwjgl2XboxAgent.class) {
            if (legacyControllablePointerFailureLogCount < 12) {
                legacyControllablePointerFailureLogCount++;
                log("Controllable pointer " + operation + " failed: " + ex);
            }
        }
    }

    private static long getFunctionPointer(Object library, String name) {
        try {
            Class<?> nativeLibraryClass = Class.forName("com.sun.jna.NativeLibrary");
            Object function = nativeLibraryClass.getMethod("getFunction", String.class).invoke(library, name);
            return pointerValue(function);
        } catch (Throwable ignored) {
            return 0L;
        }
    }

    private static long callWglGetProcAddress(Object library, String name) {
        try {
            Object function = openGlWglGetProcAddressFunction;
            if (function == null) {
                synchronized (Lwjgl2XboxAgent.class) {
                    function = openGlWglGetProcAddressFunction;
                    if (function == null) {
                        Class<?> nativeLibraryClass = Class.forName("com.sun.jna.NativeLibrary");
                        function = nativeLibraryClass.getMethod("getFunction", String.class).invoke(library, "wglGetProcAddress");
                        openGlWglGetProcAddressFunction = function;
                    }
                }
            }

            Object pointer = function.getClass().getMethod("invokePointer", Object[].class).invoke(
                    function,
                    new Object[] { new Object[] { name } });
            return pointerValue(pointer);
        } catch (Throwable ignored) {
            return 0L;
        }
    }

    private static long pointerValue(Object pointer) {
        if (pointer == null) {
            return 0L;
        }
        try {
            Class<?> pointerClass = Class.forName("com.sun.jna.Pointer");
            Object value = pointerClass.getMethod("nativeValue", pointerClass).invoke(null, pointer);
            return ((Number)value).longValue();
        } catch (Throwable ignored) {
            return 0L;
        }
    }

    private static boolean shouldLogOpenGlResolve(String name) {
        return "glGetString".equals(name)
                || "glGetError".equals(name)
                || "glClearColor".equals(name)
                || "glClear".equals(name)
                || "glViewport".equals(name)
                || "glDrawArrays".equals(name)
                || "glDrawElements".equals(name)
                || "glMatrixMode".equals(name)
                || "glLoadIdentity".equals(name)
                || "glOrtho".equals(name)
                || "glPushMatrix".equals(name)
                || "glPopMatrix".equals(name)
                || "glTranslatef".equals(name)
                || "glScalef".equals(name)
                || "glRotatef".equals(name)
                || "glVertexPointer".equals(name)
                || "glTexCoordPointer".equals(name)
                || "glColorPointer".equals(name)
                || "glEnableClientState".equals(name)
                || "glDisableClientState".equals(name)
                || "glBlendFunc".equals(name)
                || "glGenLists".equals(name)
                || "glNewList".equals(name)
                || "glEndList".equals(name)
                || "glCallList".equals(name)
                || "glCallLists".equals(name)
                || "glDeleteLists".equals(name)
                || "glIsList".equals(name)
                || "glListBase".equals(name)
                || "glBindTexture".equals(name)
                || "glTexImage2D".equals(name)
                || "glTexSubImage2D".equals(name)
                || "glFlush".equals(name);
    }

    private static final class Transformer implements ClassFileTransformer {
        @Override
        public byte[] transform(
                ClassLoader loader,
                String className,
                Class<?> classBeingRedefined,
                ProtectionDomain protectionDomain,
                byte[] classfileBuffer) {
            boolean legacyMultiplayerScreen = false;
            boolean legacyLanServerList = false;
            boolean legacyLanDetectorThread = false;
            boolean legacyConnectScreen = false;
            boolean legacyMarkerScan = shouldScanLegacyMinecraftClass(className);
            boolean possibleLegacyPatch = legacyMarkerScan
                    || isLegacyMultiplayerScreen(className)
                    || isLegacyLanServerList(className)
                    || isLegacyLanDetectorThread(className)
                    || isLegacyConnectScreen(className);
            if (possibleLegacyPatch && Boolean.getBoolean(LAN_PLAY_PROPERTY)) {
                legacyMultiplayerScreen = isLegacyMultiplayerScreen(className);
                legacyLanServerList = isLegacyLanServerList(className);
                legacyLanDetectorThread = isLegacyLanDetectorThread(className);
                legacyConnectScreen = isLegacyConnectScreen(className);
                if (legacyMarkerScan && !legacyLanDetectorThread) {
                    legacyLanDetectorThread = hasClassText(classfileBuffer, "LanServerDetector #");
                }
                if (legacyMarkerScan && !legacyConnectScreen) {
                    legacyConnectScreen = hasClassText(classfileBuffer, "Connecting to {}, {}")
                            && hasClassText(classfileBuffer, "Server Connector #");
                }
            }

            if (!WINDOWS_DISPLAY.equals(className)
                    && !WINDOWS_CONTEXT_IMPLEMENTATION.equals(className)
                    && !GL_CONTEXT.equals(className)
                    && !LAUNCH_WRAPPER.equals(className)
                    && !legacyMultiplayerScreen
                    && !legacyLanServerList
                    && !legacyLanDetectorThread
                    && !legacyConnectScreen
                    && !(CONTROLLABLE_CONTROLLER_MANAGER.equals(className) && Boolean.getBoolean(DISABLE_CONTROLLABLE_SDL2_PROPERTY))
                    && !(CONTROLLABLE_CONTROLLER_INPUT.equals(className) && Boolean.getBoolean(DISABLE_CONTROLLABLE_SDL2_PROPERTY))
                    && !(CONTROLLABLE_GUI_EVENTS.equals(className) && Boolean.getBoolean(DISABLE_CONTROLLABLE_SDL2_PROPERTY))
                    && !(SDL2_CONTROLLER.equals(className) && Boolean.getBoolean(DISABLE_CONTROLLABLE_SDL2_PROPERTY))
                    && !(SDL2_CONTROLLER_MANAGER.equals(className) && Boolean.getBoolean(DISABLE_CONTROLLABLE_SDL2_PROPERTY))) {
                return null;
            }

            try {
                ClassReader reader = new ClassReader(classfileBuffer);
                ClassWriter writer = createClassWriter(reader, className);
                if (LAUNCH_WRAPPER.equals(className)) {
                    reader.accept(new LaunchWrapperVisitor(writer), 0);
                    log("patched net.minecraft.launchwrapper.Launch");
                } else if (legacyMultiplayerScreen) {
                    reader.accept(new LegacyMultiplayerScreenVisitor(writer), 0);
                    log("patched legacy multiplayer screen class=" + className);
                } else if (legacyLanServerList) {
                    reader.accept(new LegacyLanServerListVisitor(writer), 0);
                    log("patched legacy LAN server list class=" + className);
                } else if (legacyLanDetectorThread) {
                    reader.accept(new LegacyLanDetectorThreadVisitor(writer), 0);
                    log("patched legacy LAN detector thread class=" + className);
                } else if (legacyConnectScreen) {
                    reader.accept(new LegacyConnectScreenVisitor(writer), 0);
                    log("patched legacy direct connect screen class=" + className);
                } else if (CONTROLLABLE_CONTROLLER_MANAGER.equals(className)) {
                    reader.accept(new ControllableControllerManagerVisitor(writer), 0);
                    log("patched Controllable controller manager");
                } else if (CONTROLLABLE_CONTROLLER_INPUT.equals(className)) {
                    reader.accept(new ControllableControllerInputVisitor(writer), 0);
                    log("patched Controllable controller input pointer toggle");
                } else if (CONTROLLABLE_GUI_EVENTS.equals(className)) {
                    reader.accept(new ControllableGuiEventsVisitor(writer), 0);
                    log("patched Controllable GUI pointer overlay");
                } else if (SDL2_CONTROLLER.equals(className)) {
                    reader.accept(new Sdl2ControllerVisitor(writer), 0);
                    log("patched Controllable SDL2 controller");
                } else if (SDL2_CONTROLLER_MANAGER.equals(className)) {
                    reader.accept(new Sdl2ControllerManagerVisitor(writer), 0);
                    log("patched Controllable SDL2 controller manager");
                } else if (WINDOWS_DISPLAY.equals(className)) {
                    reader.accept(new WindowsDisplayVisitor(writer), 0);
                    log("patched org.lwjgl.opengl.WindowsDisplay");
                } else if (WINDOWS_CONTEXT_IMPLEMENTATION.equals(className)) {
                    reader.accept(new WindowsContextImplementationVisitor(writer), 0);
                    log("patched org.lwjgl.opengl.WindowsContextImplementation");
                } else if (GL_CONTEXT.equals(className)) {
                    reader.accept(new GlContextVisitor(writer), 0);
                    log("patched org.lwjgl.opengl.GLContext");
                }
                return writer.toByteArray();
            } catch (Throwable ex) {
                log(className + " patch failed: " + ex);
                return null;
            }
        }

        private static boolean isLegacyMultiplayerScreen(String className) {
            return LEGACY_MULTIPLAYER_SCREEN.equals(className);
        }

        private static boolean isLegacyLanServerList(String className) {
            return LEGACY_LAN_SERVER_LIST.equals(className)
                    || LEGACY_LAN_SERVER_LIST_DEOBF.equals(className)
                    || (className != null && className.endsWith("$LanServerList"));
        }

        private static boolean isLegacyLanDetectorThread(String className) {
            return LEGACY_LAN_DETECTOR_THREAD.equals(className)
                    || LEGACY_LAN_DETECTOR_THREAD_DEOBF.equals(className)
                    || (className != null && className.endsWith("$ThreadLanServerFind"));
        }

        private static boolean isLegacyConnectScreen(String className) {
            return LEGACY_CONNECT_SCREEN.equals(className);
        }

        private static boolean shouldScanLegacyMinecraftClass(String className) {
            if (className == null || className.length() == 0) {
                return false;
            }

            if (className.indexOf('/') < 0) {
                return true;
            }

            return className.startsWith("net/minecraft/");
        }

        private static boolean hasClassText(byte[] classfileBuffer, String needle) {
            if (classfileBuffer == null || needle == null || needle.length() == 0) {
                return false;
            }

            int needleLength = needle.length();
            outer:
            for (int i = 0; i <= classfileBuffer.length - needleLength; i++) {
                for (int j = 0; j < needleLength; j++) {
                    char ch = needle.charAt(j);
                    if (ch > 255 || (classfileBuffer[i + j] & 0xff) != ch) {
                        continue outer;
                    }
                }
                return true;
            }
            return false;
        }

        private static ClassWriter createClassWriter(ClassReader reader, String className) {
            int flags = ClassWriter.COMPUTE_MAXS;
            if (CONTROLLABLE_CONTROLLER_MANAGER.equals(className)
                    || SDL2_CONTROLLER_MANAGER.equals(className)
                    || SDL2_CONTROLLER.equals(className)) {
                flags |= ClassWriter.COMPUTE_FRAMES;
            }
            return new SafeClassWriter(reader, flags);
        }
    }

    private static final class LegacyMultiplayerScreenVisitor extends ClassVisitor {
        LegacyMultiplayerScreenVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
            if (!"e".equals(name) || !"()V".equals(descriptor)) {
                return mv;
            }

            return new MethodVisitor(Opcodes.ASM5, mv) {
                @Override
                public void visitCode() {
                    super.visitCode();
                    mv.visitVarInsn(Opcodes.ALOAD, 0);
                    mv.visitFieldInsn(Opcodes.GETFIELD, LEGACY_MULTIPLAYER_SCREEN, "B", "Lchg$b;");
                    mv.visitMethodInsn(
                            Opcodes.INVOKESTATIC,
                            "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                            "startLegacyManualLanServerInjector",
                            "(Ljava/lang/Object;)V",
                            false);
                }
            };
        }
    }

    private static final class LegacyLanServerListVisitor extends ClassVisitor {
        LegacyLanServerListVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
            if (!"<init>".equals(name) || !"()V".equals(descriptor)) {
                return mv;
            }

            return new MethodVisitor(Opcodes.ASM5, mv) {
                @Override
                public void visitInsn(int opcode) {
                    if (opcode == Opcodes.RETURN) {
                        mv.visitVarInsn(Opcodes.ALOAD, 0);
                        mv.visitMethodInsn(
                                Opcodes.INVOKESTATIC,
                                "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                                "startLegacyManualLanServerInjector",
                                "(Ljava/lang/Object;)V",
                                false);
                    }
                    super.visitInsn(opcode);
                }
            };
        }
    }

    private static final class LegacyLanDetectorThreadVisitor extends ClassVisitor {
        LegacyLanDetectorThreadVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
            if (!"<init>".equals(name)) {
                return mv;
            }

            return new MethodVisitor(Opcodes.ASM5, mv) {
                @Override
                public void visitInsn(int opcode) {
                    if (opcode == Opcodes.RETURN) {
                        mv.visitVarInsn(Opcodes.ALOAD, 1);
                        mv.visitMethodInsn(
                                Opcodes.INVOKESTATIC,
                                "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                                "startLegacyManualLanServerInjector",
                                "(Ljava/lang/Object;)V",
                                false);
                    }
                    super.visitInsn(opcode);
                }
            };
        }
    }

    private static final class LegacyConnectScreenVisitor extends ClassVisitor {
        LegacyConnectScreenVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
            if (!"a".equals(name) || !"(Ljava/lang/String;I)V".equals(descriptor)
                    || (access & Opcodes.ACC_PRIVATE) == 0) {
                return mv;
            }

            return new MethodVisitor(Opcodes.ASM5, mv) {
                @Override
                public void visitCode() {
                    super.visitCode();
                    mv.visitVarInsn(Opcodes.ALOAD, 1);
                    mv.visitVarInsn(Opcodes.ILOAD, 2);
                    mv.visitMethodInsn(
                            Opcodes.INVOKESTATIC,
                            "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                            "logLegacyDirectConnect",
                            "(Ljava/lang/String;I)V",
                            false);
                }
            };
        }
    }

    private static final class SafeClassWriter extends ClassWriter {
        SafeClassWriter(ClassReader reader, int flags) {
            super(reader, flags);
        }

        @Override
        protected String getCommonSuperClass(String type1, String type2) {
            return "java/lang/Object";
        }
    }

    private static final class ControllableControllerInputVisitor extends ClassVisitor {
        ControllableControllerInputVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
            boolean pinPointerUse =
                    "onClientTick".equals(name)
                            && "(Lnet/minecraftforge/fml/common/gameevent/TickEvent$ClientTickEvent;)V".equals(descriptor);
            boolean blockWorldControls =
                    ("onRender".equals(name)
                            && "(Lnet/minecraftforge/fml/common/gameevent/TickEvent$ClientTickEvent;)V".equals(descriptor))
                            || ("onInputUpdate".equals(name)
                            && "(Lnet/minecraftforge/client/event/InputUpdateEvent;)V".equals(descriptor));
            return new ControllableControllerInputMethodVisitor(mv, pinPointerUse, blockWorldControls);
        }
    }

    private static final class ControllableControllerInputMethodVisitor extends MethodVisitor {
        private final boolean pinPointerUse;
        private final boolean blockWorldControls;

        ControllableControllerInputMethodVisitor(MethodVisitor visitor, boolean pinPointerUse, boolean blockWorldControls) {
            super(Opcodes.ASM5, visitor);
            this.pinPointerUse = pinPointerUse;
            this.blockWorldControls = blockWorldControls;
        }

        @Override
        public void visitCode() {
            super.visitCode();
            if (pinPointerUse) {
                mv.visitVarInsn(Opcodes.ALOAD, 0);
                mv.visitMethodInsn(
                        Opcodes.INVOKESTATIC,
                        "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                        "tickLegacyControllablePointer",
                        "(Ljava/lang/Object;)V",
                        false);
            }

            if (blockWorldControls) {
                Label normal = new Label();
                mv.visitMethodInsn(
                        Opcodes.INVOKESTATIC,
                        "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                        "isLegacyControllablePointerModeActive",
                        "()Z",
                        false);
                mv.visitJumpInsn(Opcodes.IFEQ, normal);
                mv.visitInsn(Opcodes.RETURN);
                mv.visitLabel(normal);
                mv.visitFrame(Opcodes.F_SAME, 0, null, 0, null);
            }

            if (pinPointerUse) {
                Label normal = new Label();
                mv.visitMethodInsn(
                        Opcodes.INVOKESTATIC,
                        "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                        "isLegacyControllablePointerModeActive",
                        "()Z",
                        false);
                mv.visitJumpInsn(Opcodes.IFEQ, normal);
                mv.visitVarInsn(Opcodes.ALOAD, 0);
                mv.visitIntInsn(Opcodes.BIPUSH, 100);
                mv.visitFieldInsn(Opcodes.PUTFIELD, CONTROLLABLE_CONTROLLER_INPUT, "lastUse", "I");
                mv.visitLabel(normal);
                mv.visitFrame(Opcodes.F_SAME, 0, null, 0, null);
            }
        }

        @Override
        public void visitMethodInsn(int opcode, String owner, String name, String descriptor, boolean isInterface) {
            super.visitMethodInsn(opcode, owner, name, descriptor, isInterface);
            if (opcode == Opcodes.INVOKESTATIC
                    && "org/lwjgl/input/Mouse".equals(owner)
                    && "isGrabbed".equals(name)
                    && "()Z".equals(descriptor)) {
                mv.visitMethodInsn(
                        Opcodes.INVOKESTATIC,
                        "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                        "legacyControllableShouldTreatMouseGrabbed",
                        "(Z)Z",
                        false);
            }
        }
    }

    private static final class ControllableGuiEventsVisitor extends ClassVisitor {
        ControllableGuiEventsVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
            if ("onRenderOverlay".equals(name)
                    && "(Lnet/minecraftforge/client/event/RenderGameOverlayEvent$Post;)V".equals(descriptor)) {
                return new MethodVisitor(Opcodes.ASM5, mv) {
                    @Override
                    public void visitInsn(int opcode) {
                        if (opcode == Opcodes.RETURN) {
                            mv.visitVarInsn(Opcodes.ALOAD, 1);
                            mv.visitMethodInsn(
                                    Opcodes.INVOKESTATIC,
                                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                                    "renderLegacyControllablePointerOverlay",
                                    "(Ljava/lang/Object;)V",
                                    false);
                        }
                        super.visitInsn(opcode);
                    }
                };
            }

            return mv;
        }
    }

    private static final class ControllableControllerManagerVisitor extends ClassVisitor {
        ControllableControllerManagerVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("update".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitUpdate(mv);
                return null;
            }
            if ("getName".equals(name) && "(I)Ljava/lang/String;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetName(mv);
                return null;
            }
            if ("getSDL2ControllerById".equals(name) && "(I)Luk/co/electronstudio/sdl2gdx/SDL2Controller;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetSdl2ControllerById(mv);
                return null;
            }
            if ("getFirstControllerJid".equals(name) && "()I".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetFirstControllerJid(mv);
                return null;
            }
            if ("getControllers".equals(name) && "()Ljava/util/Map;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetControllers(mv);
                return null;
            }
            if ("getControllerCount".equals(name) && "()I".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetControllerCount(mv);
                return null;
            }
            if ("close".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }

            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }

        private MethodVisitor replace(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            return super.visitMethod(access & ~Opcodes.ACC_ABSTRACT, name, descriptor, signature, exceptions);
        }

        private static void emitUpdate(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, CONTROLLABLE_CONTROLLER_MANAGER, "controllers", "Ljava/util/Map;");
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, CONTROLLABLE_CONTROLLER_MANAGER, "listeners", "Ljava/util/Set;");
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "updateLegacyControllableControllers",
                    "(Ljava/util/Map;Ljava/util/Set;)V",
                    false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetName(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, CONTROLLABLE_CONTROLLER_MANAGER, "controllers", "Ljava/util/Map;");
            mv.visitVarInsn(Opcodes.ILOAD, 1);
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "getLegacyControllableControllerName",
                    "(Ljava/util/Map;I)Ljava/lang/String;",
                    false);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetSdl2ControllerById(MethodVisitor mv) {
            mv.visitCode();
            Label missing = new Label();
            mv.visitVarInsn(Opcodes.ILOAD, 1);
            mv.visitJumpInsn(Opcodes.IFNE, missing);
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, CONTROLLABLE_CONTROLLER_MANAGER, "manager", "Luk/co/electronstudio/sdl2gdx/SDL2ControllerManager;");
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "getLegacyControllableController",
                    "(Ljava/lang/Object;)Ljava/lang/Object;",
                    false);
            mv.visitTypeInsn(Opcodes.CHECKCAST, SDL2_CONTROLLER);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitLabel(missing);
            mv.visitInsn(Opcodes.ACONST_NULL);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetFirstControllerJid(MethodVisitor mv) {
            mv.visitCode();
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "getLegacyControllableFirstControllerJid",
                    "()I",
                    false);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetControllers(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, CONTROLLABLE_CONTROLLER_MANAGER, "controllers", "Ljava/util/Map;");
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "getLegacyControllableControllers",
                    "(Ljava/util/Map;)Ljava/util/Map;",
                    false);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetControllerCount(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, CONTROLLABLE_CONTROLLER_MANAGER, "controllers", "Ljava/util/Map;");
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "getLegacyControllableControllerCount",
                    "(Ljava/util/Map;)I",
                    false);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitReturn(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }
    }

    private static final class Sdl2ControllerVisitor extends ClassVisitor {
        Sdl2ControllerVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("<init>".equals(name) && "(Luk/co/electronstudio/sdl2gdx/SDL2ControllerManager;I)V".equals(descriptor)) {
                MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
                emitInit(mv);
                return null;
            }
            if ("isConnected".equals(name) && "()Z".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitIsConnected(mv);
                return null;
            }
            if ("pollState".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("getButton".equals(name) && "(I)Z".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetButton(mv);
                return null;
            }
            if ("getAxis".equals(name) && "(I)F".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetAxis(mv);
                return null;
            }
            if ("getPov".equals(name) && "(I)Lcom/badlogic/gdx/controllers/PovDirection;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitGetPov(mv);
                return null;
            }
            if ("getName".equals(name) && "()Ljava/lang/String;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitString(mv, "Xbox Gamepad");
                return null;
            }
            if ("toString".equals(name) && "()Ljava/lang/String;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitString(mv, "Xbox Gamepad instance:0 guid:xbox-uwp-gamepad");
                return null;
            }
            if ("close".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("rumble".equals(name) && "(FFI)Z".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitBoolean(mv, false);
                return null;
            }
            if ("getPowerLevel".equals(name) && "()Luk/co/electronstudio/sdl2gdx/SDL2Controller$PowerLevel;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitEnum(mv, "uk/co/electronstudio/sdl2gdx/SDL2Controller$PowerLevel", "WIRED");
                return null;
            }
            if ("getType".equals(name) && "()Luk/co/electronstudio/sdl2gdx/SDL2Controller$ControllerType;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitEnum(mv, "uk/co/electronstudio/sdl2gdx/SDL2Controller$ControllerType", "XBOXONE");
                return null;
            }
            if ("getPlayerIndex".equals(name) && "()I".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitInt(mv, 0);
                return null;
            }

            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }

        private MethodVisitor replace(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            return super.visitMethod(access & ~Opcodes.ACC_ABSTRACT, name, descriptor, signature, exceptions);
        }

        private static void emitInit(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "java/lang/Object", "<init>", "()V", false);

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitTypeInsn(Opcodes.NEW, "com/badlogic/gdx/utils/Array");
            mv.visitInsn(Opcodes.DUP);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "com/badlogic/gdx/utils/Array", "<init>", "()V", false);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "listeners", "Lcom/badlogic/gdx/utils/Array;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ALOAD, 1);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "manager", "Luk/co/electronstudio/sdl2gdx/SDL2ControllerManager;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ILOAD, 2);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "device_index", "I");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitInsn(Opcodes.ACONST_NULL);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "joystick", "Lorg/libsdl/SDL_Joystick;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitInsn(Opcodes.ACONST_NULL);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "controller", "Lorg/libsdl/SDL_GameController;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitIntInsn(Opcodes.BIPUSH, 6);
            mv.visitIntInsn(Opcodes.NEWARRAY, Opcodes.T_FLOAT);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "axisState", "[F");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitIntInsn(Opcodes.BIPUSH, 15);
            mv.visitIntInsn(Opcodes.NEWARRAY, Opcodes.T_BOOLEAN);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "buttonState", "[Z");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitInsn(Opcodes.ICONST_1);
            mv.visitTypeInsn(Opcodes.ANEWARRAY, "com/badlogic/gdx/controllers/PovDirection");
            mv.visitInsn(Opcodes.DUP);
            mv.visitInsn(Opcodes.ICONST_0);
            mv.visitFieldInsn(Opcodes.GETSTATIC, "com/badlogic/gdx/controllers/PovDirection", "center", "Lcom/badlogic/gdx/controllers/PovDirection;");
            mv.visitInsn(Opcodes.AASTORE);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER, "hatState", "[Lcom/badlogic/gdx/controllers/PovDirection;");

            mv.visitFieldInsn(Opcodes.GETSTATIC, "java/lang/System", "out", "Ljava/io/PrintStream;");
            mv.visitLdcInsn("[LWJGL2-Xbox] Controllable synthetic SDL2 controller created");
            mv.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "java/io/PrintStream", "println", "(Ljava/lang/String;)V", false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitIsConnected(MethodVisitor mv) {
            mv.visitCode();
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "isLegacyControllableControllerPresent",
                    "()Z",
                    false);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetButton(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ILOAD, 1);
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "legacyControllableButton",
                    "(I)Z",
                    false);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetAxis(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ILOAD, 1);
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "legacyControllableAxis",
                    "(I)F",
                    false);
            mv.visitInsn(Opcodes.FRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitGetPov(MethodVisitor mv) {
            mv.visitCode();
            Label down = new Label();
            Label right = new Label();
            Label left = new Label();
            Label center = new Label();

            pushButton(mv, 11);
            mv.visitJumpInsn(Opcodes.IFEQ, down);
            pushButton(mv, 14);
            Label northNotRight = new Label();
            mv.visitJumpInsn(Opcodes.IFEQ, northNotRight);
            emitPovReturn(mv, "northEast");
            mv.visitLabel(northNotRight);
            pushButton(mv, 13);
            Label northOnly = new Label();
            mv.visitJumpInsn(Opcodes.IFEQ, northOnly);
            emitPovReturn(mv, "northWest");
            mv.visitLabel(northOnly);
            emitPovReturn(mv, "north");

            mv.visitLabel(down);
            pushButton(mv, 12);
            mv.visitJumpInsn(Opcodes.IFEQ, right);
            pushButton(mv, 14);
            Label southNotRight = new Label();
            mv.visitJumpInsn(Opcodes.IFEQ, southNotRight);
            emitPovReturn(mv, "southEast");
            mv.visitLabel(southNotRight);
            pushButton(mv, 13);
            Label southOnly = new Label();
            mv.visitJumpInsn(Opcodes.IFEQ, southOnly);
            emitPovReturn(mv, "southWest");
            mv.visitLabel(southOnly);
            emitPovReturn(mv, "south");

            mv.visitLabel(right);
            pushButton(mv, 14);
            mv.visitJumpInsn(Opcodes.IFEQ, left);
            emitPovReturn(mv, "east");

            mv.visitLabel(left);
            pushButton(mv, 13);
            mv.visitJumpInsn(Opcodes.IFEQ, center);
            emitPovReturn(mv, "west");

            mv.visitLabel(center);
            emitPovReturn(mv, "center");
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void pushButton(MethodVisitor mv, int button) {
            mv.visitIntInsn(Opcodes.BIPUSH, button);
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "legacyControllableButton",
                    "(I)Z",
                    false);
        }

        private static void emitPovReturn(MethodVisitor mv, String name) {
            mv.visitFieldInsn(Opcodes.GETSTATIC, "com/badlogic/gdx/controllers/PovDirection", name, "Lcom/badlogic/gdx/controllers/PovDirection;");
            mv.visitInsn(Opcodes.ARETURN);
        }

        private static void emitEnum(MethodVisitor mv, String owner, String name) {
            mv.visitCode();
            mv.visitFieldInsn(Opcodes.GETSTATIC, owner, name, "L" + owner + ";");
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitReturn(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitBoolean(MethodVisitor mv, boolean value) {
            mv.visitCode();
            mv.visitInsn(value ? Opcodes.ICONST_1 : Opcodes.ICONST_0);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitInt(MethodVisitor mv, int value) {
            mv.visitCode();
            mv.visitInsn(value == 0 ? Opcodes.ICONST_0 : Opcodes.ICONST_M1);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitString(MethodVisitor mv, String value) {
            mv.visitCode();
            mv.visitLdcInsn(value);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }
    }

    private static final class Sdl2ControllerManagerVisitor extends ClassVisitor {
        Sdl2ControllerManagerVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("<init>".equals(name) && "(Luk/co/electronstudio/sdl2gdx/SDL2ControllerManager$InputPreference;)V".equals(descriptor)) {
                MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
                emitSdl2ControllerManagerInit(mv);
                return null;
            }
            if ("pollState".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("close".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }

            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }

        private static void emitSdl2ControllerManagerInit(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "java/lang/Object", "<init>", "()V", false);

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitTypeInsn(Opcodes.NEW, "com/badlogic/gdx/utils/Array");
            mv.visitInsn(Opcodes.DUP);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "com/badlogic/gdx/utils/Array", "<init>", "()V", false);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER_MANAGER, "controllers", "Lcom/badlogic/gdx/utils/Array;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitTypeInsn(Opcodes.NEW, "com/badlogic/gdx/utils/Array");
            mv.visitInsn(Opcodes.DUP);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "com/badlogic/gdx/utils/Array", "<init>", "()V", false);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER_MANAGER, "polledControllers", "Lcom/badlogic/gdx/utils/Array;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitTypeInsn(Opcodes.NEW, "com/badlogic/gdx/utils/IntArray");
            mv.visitInsn(Opcodes.DUP);
            mv.visitIntInsn(Opcodes.SIPUSH, 128);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "com/badlogic/gdx/utils/IntArray", "<init>", "(I)V", false);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER_MANAGER, "connectedInstanceIds", "Lcom/badlogic/gdx/utils/IntArray;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitTypeInsn(Opcodes.NEW, "com/badlogic/gdx/utils/Array");
            mv.visitInsn(Opcodes.DUP);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "com/badlogic/gdx/utils/Array", "<init>", "()V", false);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER_MANAGER, "listeners", "Lcom/badlogic/gdx/utils/Array;");

            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitInsn(Opcodes.ICONST_0);
            mv.visitFieldInsn(Opcodes.PUTFIELD, SDL2_CONTROLLER_MANAGER, "running", "Z");

            mv.visitFieldInsn(Opcodes.GETSTATIC, "java/lang/System", "out", "Ljava/io/PrintStream;");
            mv.visitLdcInsn("[LWJGL2-Xbox] Controllable SDL2 native controller backend disabled on UWP");
            mv.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "java/io/PrintStream", "println", "(Ljava/lang/String;)V", false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitReturn(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }
    }

    private static final class LaunchWrapperVisitor extends ClassVisitor {
        LaunchWrapperVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
            if ("launch".equals(name) && "([Ljava/lang/String;)V".equals(descriptor)) {
                return new LaunchWrapperLaunchVisitor(mv);
            }

            return mv;
        }
    }

    private static final class LaunchWrapperLaunchVisitor extends MethodVisitor {
        LaunchWrapperLaunchVisitor(MethodVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public void visitMethodInsn(int opcode, String owner, String name, String descriptor, boolean isInterface) {
            if (opcode == Opcodes.INVOKESTATIC
                    && "java/lang/System".equals(owner)
                    && "exit".equals(name)
                    && "(I)V".equals(descriptor)) {
                super.visitInsn(Opcodes.POP);
                super.visitVarInsn(Opcodes.ALOAD, 14);
                super.visitInsn(Opcodes.ATHROW);
                log("patched LaunchWrapper System.exit trap");
                return;
            }

            super.visitMethodInsn(opcode, owner, name, descriptor, isInterface);
        }
    }

    private static final class GlContextVisitor extends ClassVisitor {
        GlContextVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("getFunctionAddress".equals(name) && "(Ljava/lang/String;)J".equals(descriptor)) {
                MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
                mv.visitCode();
                mv.visitVarInsn(Opcodes.ALOAD, 0);
                mv.visitMethodInsn(
                        Opcodes.INVOKESTATIC,
                        "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                        "resolveOpenGlFunctionAddress",
                        "(Ljava/lang/String;)J",
                        false);
                mv.visitInsn(Opcodes.LRETURN);
                mv.visitMaxs(0, 0);
                mv.visitEnd();
                return null;
            }

            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }
    }

    private static final class WindowsDisplayVisitor extends ClassVisitor {
        WindowsDisplayVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("init".equals(name) && "()Lorg/lwjgl/opengl/DisplayMode;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitInit(mv);
                return null;
            }
            if ("getAvailableDisplayModes".equals(name) && "()[Lorg/lwjgl/opengl/DisplayMode;".equals(descriptor)) {
                MethodVisitor mv = replace(access & ~Opcodes.ACC_NATIVE, name, descriptor, signature, exceptions);
                emitAvailableDisplayModes(mv);
                return null;
            }
            if ("createWindow".equals(name)
                    && "(Lorg/lwjgl/opengl/DrawableLWJGL;Lorg/lwjgl/opengl/DisplayMode;Ljava/awt/Canvas;II)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitCreateWindow(mv);
                return null;
            }
            if ("reshape".equals(name) && "(IIII)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReshape(mv);
                return null;
            }
            if ("switchDisplayMode".equals(name) && "(Lorg/lwjgl/opengl/DisplayMode;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitSwitchDisplayMode(mv);
                return null;
            }
            if ("createPeerInfo".equals(name)
                    && "(Lorg/lwjgl/opengl/PixelFormat;Lorg/lwjgl/opengl/ContextAttribs;)Lorg/lwjgl/opengl/PeerInfo;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitNull(mv);
                return null;
            }
            if ("setResizable".equals(name) && "(Z)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitSetBooleanField(mv, "resizable");
                return null;
            }
            if ("createKeyboard".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitCreateLegacyKeyboard(mv);
                return null;
            }
            if ("destroyKeyboard".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitDestroyLegacyKeyboard(mv);
                return null;
            }
            if ("pollKeyboard".equals(name) && "(Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitPollLegacyKeyboard(mv);
                return null;
            }
            if ("readKeyboard".equals(name) && "(Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReadLegacyKeyboard(mv);
                return null;
            }
            if ("createMouse".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("destroyMouse".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("pollMouse".equals(name) && "(Ljava/nio/IntBuffer;Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("readMouse".equals(name) && "(Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("grabMouse".equals(name) && "(Z)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("setCursorPosition".equals(name) && "(II)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("hasWheel".equals(name) && "()Z".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitBoolean(mv, false);
                return null;
            }
            if ("getButtonCount".equals(name) && "()I".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitInt(mv, 0);
                return null;
            }
            if (isNoOpVoid(name, descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if (isFalseBoolean(name, descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitBoolean(mv, false);
                return null;
            }
            if (isTrueBoolean(name, descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitBoolean(mv, true);
                return null;
            }
            if (isZeroInt(name, descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitInt(mv, 0);
                return null;
            }
            if ("setIcon".equals(name) && "([Ljava/nio/ByteBuffer;)I".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitInt(mv, 0);
                return null;
            }
            if ("getPixelScaleFactor".equals(name) && "()F".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitFloatOne(mv);
                return null;
            }
            if ("createCursor".equals(name)
                    && "(IIIIILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)Ljava/lang/Object;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitNull(mv);
                return null;
            }
            if ("createPbuffer".equals(name)
                    && "(IILorg/lwjgl/opengl/PixelFormat;Lorg/lwjgl/opengl/ContextAttribs;Ljava/nio/IntBuffer;Ljava/nio/IntBuffer;)Lorg/lwjgl/opengl/PeerInfo;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitNull(mv);
                return null;
            }
            if ("getAdapter".equals(name) && "()Ljava/lang/String;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitString(mv, "Xbox UWP");
                return null;
            }
            if ("getVersion".equals(name) && "()Ljava/lang/String;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitString(mv, "LWJGL2 Xbox compatibility");
                return null;
            }

            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }

        private MethodVisitor replace(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            return super.visitMethod(access & ~Opcodes.ACC_ABSTRACT, name, descriptor, signature, exceptions);
        }

        private static boolean isNoOpVoid(String name, String descriptor) {
            if ("destroyWindow".equals(name) && "()V".equals(descriptor)) return true;
            if ("resetDisplayMode".equals(name) && "()V".equals(descriptor)) return true;
            if ("setGammaRamp".equals(name) && "(Ljava/nio/FloatBuffer;)V".equals(descriptor)) return true;
            if ("setTitle".equals(name) && "(Ljava/lang/String;)V".equals(descriptor)) return true;
            if ("update".equals(name) && "()V".equals(descriptor)) return true;
            if ("setNativeCursor".equals(name) && "(Ljava/lang/Object;)V".equals(descriptor)) return true;
            if ("destroyCursor".equals(name) && "(Ljava/lang/Object;)V".equals(descriptor)) return true;
            if ("setPbufferAttrib".equals(name) && "(Lorg/lwjgl/opengl/PeerInfo;II)V".equals(descriptor)) return true;
            if ("bindTexImageToPbuffer".equals(name) && "(Lorg/lwjgl/opengl/PeerInfo;I)V".equals(descriptor)) return true;
            return "releaseTexImageFromPbuffer".equals(name) && "(Lorg/lwjgl/opengl/PeerInfo;I)V".equals(descriptor);
        }

        private static boolean isFalseBoolean(String name, String descriptor) {
            if (!"()Z".equals(descriptor)) return false;
            return "wasResized".equals(name) || "isBufferLost".equals(name);
        }

        private static boolean isTrueBoolean(String name, String descriptor) {
            return "()Z".equals(descriptor) && "isInsideWindow".equals(name);
        }

        private static boolean isZeroInt(String name, String descriptor) {
            if (!"()I".equals(descriptor)) return false;
            return "getNativeCursorCapabilities".equals(name)
                    || "getMinCursorSize".equals(name)
                    || "getMaxCursorSize".equals(name)
                    || "getPbufferCapabilities".equals(name)
                    || "setIcon".equals(name)
                    || "getGammaRampLength".equals(name);
        }

        private static void emitInit(MethodVisitor mv) {
            mv.visitCode();
            emitNewDisplayMode(mv);
            mv.visitInsn(Opcodes.DUP);
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitInsn(Opcodes.SWAP);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, "current_mode", "Lorg/lwjgl/opengl/DisplayMode;");
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitAvailableDisplayModes(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.ICONST_1);
            mv.visitTypeInsn(Opcodes.ANEWARRAY, "org/lwjgl/opengl/DisplayMode");
            mv.visitInsn(Opcodes.DUP);
            mv.visitInsn(Opcodes.ICONST_0);
            emitNewDisplayMode(mv);
            mv.visitInsn(Opcodes.AASTORE);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitCreateWindow(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ALOAD, 3);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, "parent", "Ljava/awt/Canvas;");

            Label noParent = new Label();
            Label parentDone = new Label();
            mv.visitVarInsn(Opcodes.ALOAD, 3);
            mv.visitJumpInsn(Opcodes.IFNULL, noParent);
            mv.visitInsn(Opcodes.ICONST_1);
            mv.visitJumpInsn(Opcodes.GOTO, parentDone);
            mv.visitLabel(noParent);
            mv.visitInsn(Opcodes.ICONST_0);
            mv.visitLabel(parentDone);
            mv.visitFieldInsn(Opcodes.PUTSTATIC, WINDOWS_DISPLAY, "hasParent", "Z");

            putLongField(mv, "parent_hwnd", 0L);
            putIntLocalField(mv, "x", 4);
            putIntLocalField(mv, "y", 5);
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ALOAD, 2);
            mv.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "org/lwjgl/opengl/DisplayMode", "getWidth", "()I", false);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, "width", "I");
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ALOAD, 2);
            mv.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "org/lwjgl/opengl/DisplayMode", "getHeight", "()I", false);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, "height", "I");
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ALOAD, 2);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, "current_mode", "Lorg/lwjgl/opengl/DisplayMode;");
            putLongField(mv, "hwnd", 0x6c776a676c320001L);
            putLongField(mv, "hdc", 0x6c776a676c320002L);
            putBooleanField(mv, "isFocused", true);
            putBooleanField(mv, "is_dirty", true);
            putBooleanField(mv, "mouseInside", true);

            Label done = new Label();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, WINDOWS_DISPLAY, "peer_info", "Lorg/lwjgl/opengl/WindowsDisplayPeerInfo;");
            mv.visitJumpInsn(Opcodes.IFNULL, done);
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, WINDOWS_DISPLAY, "peer_info", "Lorg/lwjgl/opengl/WindowsDisplayPeerInfo;");
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, WINDOWS_DISPLAY, "hwnd", "J");
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitFieldInsn(Opcodes.GETFIELD, WINDOWS_DISPLAY, "hdc", "J");
            mv.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "org/lwjgl/opengl/WindowsDisplayPeerInfo", "initDC", "(JJ)V", false);
            mv.visitLabel(done);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitReshape(MethodVisitor mv) {
            mv.visitCode();
            putIntLocalField(mv, "x", 1);
            putIntLocalField(mv, "y", 2);
            putIntLocalField(mv, "width", 3);
            putIntLocalField(mv, "height", 4);
            putBooleanField(mv, "resized", true);
            putBooleanField(mv, "is_dirty", true);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitSwitchDisplayMode(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ALOAD, 1);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, "current_mode", "Lorg/lwjgl/opengl/DisplayMode;");
            putBooleanField(mv, "mode_set", true);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitSetBooleanField(MethodVisitor mv, String fieldName) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ILOAD, 1);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, fieldName, "Z");
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitCreateLegacyKeyboard(MethodVisitor mv) {
            mv.visitCode();
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "createLegacyKeyboard",
                    "()V",
                    false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitDestroyLegacyKeyboard(MethodVisitor mv) {
            mv.visitCode();
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "destroyLegacyKeyboard",
                    "()V",
                    false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitPollLegacyKeyboard(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 1);
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "pollLegacyKeyboard",
                    "(Ljava/nio/ByteBuffer;)V",
                    false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitReadLegacyKeyboard(MethodVisitor mv) {
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 1);
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "readLegacyKeyboard",
                    "(Ljava/nio/ByteBuffer;)V",
                    false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitNewDisplayMode(MethodVisitor mv) {
            mv.visitTypeInsn(Opcodes.NEW, "org/lwjgl/opengl/DisplayMode");
            mv.visitInsn(Opcodes.DUP);
            emitResolutionProperty(mv, "minecraft.window.width", 1920);
            emitResolutionProperty(mv, "minecraft.window.height", 1080);
            mv.visitMethodInsn(Opcodes.INVOKESPECIAL, "org/lwjgl/opengl/DisplayMode", "<init>", "(II)V", false);
        }

        private static void emitResolutionProperty(MethodVisitor mv, String propertyName, int fallback) {
            mv.visitLdcInsn(propertyName);
            mv.visitIntInsn(Opcodes.SIPUSH, fallback);
            mv.visitMethodInsn(Opcodes.INVOKESTATIC, "java/lang/Integer", "getInteger", "(Ljava/lang/String;I)Ljava/lang/Integer;", false);
            mv.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "java/lang/Integer", "intValue", "()I", false);
        }

        private static void putIntLocalField(MethodVisitor mv, String fieldName, int localIndex) {
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitVarInsn(Opcodes.ILOAD, localIndex);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, fieldName, "I");
        }

        private static void putLongField(MethodVisitor mv, String fieldName, long value) {
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitLdcInsn(Long.valueOf(value));
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, fieldName, "J");
        }

        private static void putBooleanField(MethodVisitor mv, String fieldName, boolean value) {
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitInsn(value ? Opcodes.ICONST_1 : Opcodes.ICONST_0);
            mv.visitFieldInsn(Opcodes.PUTFIELD, WINDOWS_DISPLAY, fieldName, "Z");
        }

        private static void emitReturn(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitBoolean(MethodVisitor mv, boolean value) {
            mv.visitCode();
            mv.visitInsn(value ? Opcodes.ICONST_1 : Opcodes.ICONST_0);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitInt(MethodVisitor mv, int value) {
            mv.visitCode();
            mv.visitInsn(value == 0 ? Opcodes.ICONST_0 : Opcodes.ICONST_M1);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitFloatOne(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.FCONST_1);
            mv.visitInsn(Opcodes.FRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitNull(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.ACONST_NULL);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitString(MethodVisitor mv, String value) {
            mv.visitCode();
            mv.visitLdcInsn(value);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }
    }

    private static final class WindowsContextImplementationVisitor extends ClassVisitor {
        WindowsContextImplementationVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM5, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("create".equals(name)
                    && "(Lorg/lwjgl/opengl/PeerInfo;Ljava/nio/IntBuffer;Ljava/nio/ByteBuffer;)Ljava/nio/ByteBuffer;".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitCreateSyntheticContext(mv);
                return null;
            }
            if ("getHGLRC".equals(name) && "(Ljava/nio/ByteBuffer;)J".equals(descriptor)) {
                MethodVisitor mv = replace(access & ~Opcodes.ACC_NATIVE, name, descriptor, signature, exceptions);
                emitLong(mv, 0x6c776a676c320101L);
                return null;
            }
            if ("getHDC".equals(name) && "(Ljava/nio/ByteBuffer;)J".equals(descriptor)) {
                MethodVisitor mv = replace(access & ~Opcodes.ACC_NATIVE, name, descriptor, signature, exceptions);
                emitLong(mv, 0x6c776a676c320102L);
                return null;
            }
            if ("swapBuffers".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitPresent(mv);
                return null;
            }
            if ("releaseDrawable".equals(name) && "(Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("update".equals(name) && "(Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("releaseCurrentContext".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("makeCurrent".equals(name) && "(Lorg/lwjgl/opengl/PeerInfo;Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("isCurrent".equals(name) && "(Ljava/nio/ByteBuffer;)Z".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitBoolean(mv, true);
                return null;
            }
            if ("setSwapInterval".equals(name) && "(I)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }
            if ("destroy".equals(name) && "(Lorg/lwjgl/opengl/PeerInfo;Ljava/nio/ByteBuffer;)V".equals(descriptor)) {
                MethodVisitor mv = replace(access, name, descriptor, signature, exceptions);
                emitReturn(mv);
                return null;
            }

            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }

        private MethodVisitor replace(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            return super.visitMethod((access & ~Opcodes.ACC_ABSTRACT) & ~Opcodes.ACC_NATIVE, name, descriptor, signature, exceptions);
        }

        private static void emitCreateSyntheticContext(MethodVisitor mv) {
            mv.visitCode();
            mv.visitIntInsn(Opcodes.BIPUSH, 32);
            mv.visitMethodInsn(Opcodes.INVOKESTATIC, "java/nio/ByteBuffer", "allocateDirect", "(I)Ljava/nio/ByteBuffer;", false);
            mv.visitInsn(Opcodes.ARETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitPresent(MethodVisitor mv) {
            mv.visitCode();
            mv.visitMethodInsn(
                    Opcodes.INVOKESTATIC,
                    "com/minecraftxbox/lwjgl2/Lwjgl2XboxAgent",
                    "presentLegacyFrame",
                    "()V",
                    false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitReturn(MethodVisitor mv) {
            mv.visitCode();
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitBoolean(MethodVisitor mv, boolean value) {
            mv.visitCode();
            mv.visitInsn(value ? Opcodes.ICONST_1 : Opcodes.ICONST_0);
            mv.visitInsn(Opcodes.IRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        private static void emitLong(MethodVisitor mv, long value) {
            mv.visitCode();
            mv.visitLdcInsn(Long.valueOf(value));
            mv.visitInsn(Opcodes.LRETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }
    }
}
