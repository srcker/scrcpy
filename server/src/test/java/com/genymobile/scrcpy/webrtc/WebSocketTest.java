package com.genymobile.scrcpy.webrtc;

import org.junit.Test;
import org.webrtc.IceCandidate;

public class WebSocketTest {

    private SignalingClient signaling;

    @Test
    public void testWebSocket() {

        String signalUrl = "wss://your-signal-server-url";

        Long userId = 1L;

        signaling = new SignalingClient(signalUrl, userId, null);
        signaling.connect();

    }
}
