package com.genymobile.scrcpy.webrtc;

import android.util.Log;

import okhttp3.*;
import okio.ByteString;

import org.jetbrains.annotations.NotNull;
import org.json.JSONObject;

import java.util.concurrent.TimeUnit;

// WebSocket 信令客户端
public class SignalingClient {
    private final String url;
    private final Long userId;
    private WebSocket ws;

    public interface SignalingListener {
        void onOffer(String sdp);
        void onAnswer(String sdp);
        void onCandidate(String sdpMid, int sdpMLineIndex, String candidate);
    }

    private final SignalingListener listener;

    public SignalingClient(String url, Long userId, SignalingListener listener) {
        this.url = url;
        this.userId = userId;
        this.listener = listener;
    }

    public void connect() {
        OkHttpClient client = new OkHttpClient.Builder()
                .readTimeout(0, TimeUnit.MILLISECONDS)
                .build();

        Request request = new Request.Builder().url(url).build();
        ws = client.newWebSocket(request, new WebSocketListener() {

            @Override
            public void onMessage(@NotNull WebSocket webSocket, @NotNull  String text) {
                try {

                    Log.d("WebSocket", "Received: " + text);

                    JSONObject msg = new JSONObject(text);
                    String type = msg.getString("type");
                    switch (type) {
                        case "offer": listener.onOffer(msg.getString("sdp")); break;
                        case "answer": listener.onAnswer(msg.getString("sdp")); break;
                        case "candidate":
                            listener.onCandidate(msg.getString("sdpMid"),
                                    msg.getInt("sdpMLineIndex"),
                                    msg.getString("candidate"));
                            break;
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        });
    }

    public void send(JSONObject msg) {
        if (ws != null) ws.send(msg.toString());
    }

    public void close() {
        if (ws != null) ws.close(1000, "bye");
    }
}
