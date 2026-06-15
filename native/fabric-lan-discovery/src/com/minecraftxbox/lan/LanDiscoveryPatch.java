package com.minecraftxbox.lan;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.lang.reflect.Method;
import java.net.DatagramPacket;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.MulticastSocket;
import java.net.NetworkInterface;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class LanDiscoveryPatch {
    private static final String BIND_ADDRESS_PROPERTY = "minecraft.xbox.lan.bindAddress";
    private static final String MANUAL_SERVERS_PROPERTY = "minecraft.xbox.lan.manualServers";
    private static final String FORWARDED_HOST_START = "[XBOXHOST]";
    private static final String FORWARDED_HOST_END = "[/XBOXHOST]";
    private static volatile boolean loggedSocketReady;
    private static volatile boolean manualInjectorStarted;
    private static volatile String lastLoggedForwardedHost;

    private LanDiscoveryPatch() {
    }

    public static void configure(MulticastSocket socket) {
        if (socket == null) {
            return;
        }

        try {
            socket.setReuseAddress(true);

            Inet4Address address = configuredAddress();
            if (address == null) {
                address = firstLanAddress();
            }

            if (address == null) {
                log("no non-loopback IPv4 address found for LAN discovery");
                return;
            }

            socket.setInterface(address);
            if (!loggedSocketReady) {
                loggedSocketReady = true;
                NetworkInterface networkInterface = NetworkInterface.getByInetAddress(address);
                log("LAN detector socket using IPv4 " + address.getHostAddress()
                    + (networkInterface == null ? "" : " on " + networkInterface.getName()));
            }
        } catch (Throwable ex) {
            log("LAN detector socket patch failed: " + ex);
        }
    }

    public static InetAddress forwardedAddress(DatagramPacket packet) {
        if (packet == null) {
            return null;
        }

        try {
            String message = new String(
                packet.getData(),
                packet.getOffset(),
                packet.getLength(),
                StandardCharsets.UTF_8);
            int start = message.indexOf(FORWARDED_HOST_START);
            if (start < 0) {
                return null;
            }

            start += FORWARDED_HOST_START.length();
            int end = message.indexOf(FORWARDED_HOST_END, start);
            if (end <= start) {
                return null;
            }

            String host = message.substring(start, end).trim();
            InetAddress address = InetAddress.getByName(host);
            if (address instanceof Inet4Address
                    && !address.isLoopbackAddress()
                    && !address.isAnyLocalAddress()) {
                if (!host.equals(lastLoggedForwardedHost)) {
                    lastLoggedForwardedHost = host;
                    log("LAN detector using forwarded host " + host);
                }
                return address;
            }
        } catch (Throwable ex) {
            log("LAN detector forwarded address parse failed: " + ex);
        }

        return null;
    }

    public static void startManualServerInjector(Object lanServerList) {
        if (lanServerList == null || manualInjectorStarted) {
            return;
        }

        manualInjectorStarted = true;
        Thread thread = new Thread(new Runnable() {
            @Override
            public void run() {
                manualServerLoop(lanServerList);
            }
        }, "Xbox manual LAN server injector");
        thread.setDaemon(true);
        thread.start();
        log("manual LAN server injector started");
    }

    private static void manualServerLoop(Object lanServerList) {
        Set<String> loggedServers = new HashSet<String>();

        Method addServer;
        try {
            addServer = lanServerList.getClass().getMethod("method_4824", String.class, InetAddress.class);
        } catch (Throwable ex) {
            log("manual LAN injector could not find LanServerList.addServer: " + ex);
            return;
        }

        while (true) {
            try {
                List<ManualServer> servers = readManualServers();
                for (ManualServer server : servers) {
                    InetAddress address = InetAddress.getByName(server.host);
                    String message = "[MOTD]" + sanitizeTagText(server.name) + "[/MOTD][AD]" + server.port + "[/AD]";
                    addServer.invoke(lanServerList, message, address);

                    String key = server.host + ":" + server.port;
                    if (loggedServers.add(key)) {
                        log("manual LAN server injected " + server.name + " at " + key);
                    }
                }
            } catch (Throwable ex) {
                log("manual LAN injector failed: " + ex);
            }

            try {
                Thread.sleep(2000L);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                return;
            }
        }
    }

    private static List<ManualServer> readManualServers() {
        List<ManualServer> servers = new ArrayList<ManualServer>();
        Set<String> seen = new HashSet<String>();

        for (File file : manualServerFiles()) {
            if (file == null || !file.isFile()) {
                continue;
            }

            try {
                List<String> lines = Files.readAllLines(file.toPath(), StandardCharsets.UTF_8);
                for (String line : lines) {
                    ManualServer server = parseManualServer(line);
                    if (server == null) {
                        continue;
                    }

                    String key = server.host + ":" + server.port;
                    if (seen.add(key)) {
                        servers.add(server);
                    }
                }
            } catch (Throwable ex) {
                log("manual LAN server file read failed " + file + ": " + ex);
            }
        }

        return servers;
    }

    private static List<File> manualServerFiles() {
        List<File> files = new ArrayList<File>();
        String configured = System.getProperty(MANUAL_SERVERS_PROPERTY, "").trim();
        if (!configured.isEmpty()) {
            String[] paths = configured.split(";");
            for (String path : paths) {
                String trimmed = path.trim();
                if (!trimmed.isEmpty()) {
                    files.add(new File(trimmed));
                }
            }
        }

        File gameDir = new File(System.getProperty("user.home", "."));
        files.add(new File(gameDir, "xbox-lan-servers.txt"));
        files.add(new File(gameDir, "lan-servers.txt"));
        files.add(new File(new File(gameDir, "config"), "xbox-lan-servers.txt"));

        File localState = gameDir.getParentFile() == null ? null : gameDir.getParentFile().getParentFile();
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
        if (text.isEmpty() || text.startsWith("#") || text.startsWith("//")) {
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

        int colon = text.lastIndexOf(':');
        if (colon <= 0 || colon >= text.length() - 1) {
            return null;
        }

        String host = text.substring(0, colon).trim();
        String portText = text.substring(colon + 1).trim();
        try {
            int port = Integer.parseInt(portText);
            if (port < 1 || port > 65535 || host.isEmpty()) {
                return null;
            }
            if (name.isEmpty()) {
                name = "PC LAN World";
            }
            return new ManualServer(name, host, port);
        } catch (NumberFormatException ex) {
            return null;
        }
    }

    private static String sanitizeTagText(String text) {
        return text == null ? "PC LAN World" : text.replace('[', '(').replace(']', ')');
    }

    private static Inet4Address configuredAddress() {
        String configured = System.getProperty(BIND_ADDRESS_PROPERTY, "").trim();
        if (configured.isEmpty()) {
            return null;
        }

        try {
            InetAddress address = InetAddress.getByName(configured);
            if (address instanceof Inet4Address
                    && !address.isLoopbackAddress()
                    && !address.isAnyLocalAddress()) {
                return (Inet4Address)address;
            }
        } catch (Throwable ex) {
            log("configured LAN IPv4 address is not usable: " + configured + " error=" + ex);
        }

        return null;
    }

    private static Inet4Address firstLanAddress() {
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface networkInterface = interfaces.nextElement();
                if (!networkInterface.isUp() || networkInterface.isLoopback()) {
                    continue;
                }

                Enumeration<InetAddress> addresses = networkInterface.getInetAddresses();
                while (addresses.hasMoreElements()) {
                    InetAddress address = addresses.nextElement();
                    if (address instanceof Inet4Address
                            && !address.isLoopbackAddress()
                            && !address.isAnyLocalAddress()) {
                        return (Inet4Address)address;
                    }
                }
            }
        } catch (Throwable ex) {
            log("LAN detector IPv4 enumeration failed: " + ex);
        }

        return null;
    }

    private static synchronized void log(String message) {
        String line = "[Xbox-LAN-Fabric] " + message;
        System.err.println(line);

        String userHome = System.getProperty("user.home", ".");
        File logDir = new File(userHome, "logs");
        if (!logDir.exists()) {
            logDir.mkdirs();
        }

        File logFile = new File(logDir, "xbox-lan-fabric.log");
        try (FileWriter writer = new FileWriter(logFile, true)) {
            writer.write(line);
            writer.write(System.lineSeparator());
        } catch (IOException ignored) {
        }
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
}
