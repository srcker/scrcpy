package com.genymobile.scrcpy.webrtc;

import android.content.Context;
import android.view.Surface;

import com.genymobile.scrcpy.Options;

import org.webrtc.DataChannel;
import org.webrtc.EglBase;
import org.webrtc.IceCandidate;
import org.webrtc.MediaConstraints;
import org.webrtc.MediaStream;
import org.webrtc.PeerConnection;
import org.webrtc.PeerConnectionFactory;
import org.webrtc.RtpReceiver;
import org.webrtc.SessionDescription;
import org.webrtc.SurfaceTextureHelper;
import org.webrtc.VideoSource;
import org.webrtc.VideoTrack;
import org.webrtc.DefaultVideoEncoderFactory;
import org.webrtc.DefaultVideoDecoderFactory;

import java.util.Collections;

// WebRTC 模块：封装 PeerConnection、VideoTrack、信令
public class WebRTCModule {

    private final String signalUrl;
    private PeerConnectionFactory factory;
    private PeerConnection pc;
    private VideoTrack videoTrack;
    private VideoSource videoSource;
    private SurfaceTextureHelper textureHelper;
    private SignalingClient signaling;
    private EglBase eglBase;

    private final Context appContext;

    private Long userId;

    public WebRTCModule(Context context, Options options) {
        this.appContext = context.getApplicationContext(); // 确保是 ApplicationContext
        this.signalUrl = options.getWebrtcSignalUrl();
        this.userId = options.getUserId();
    }


    // 初始化 WebRTC
    public void init() {
        PeerConnectionFactory.initialize(
                PeerConnectionFactory.InitializationOptions.builder(appContext)
                        .setEnableInternalTracer(true) // 可选：开启内部日志
                        .createInitializationOptions());

        eglBase = EglBase.create();

        factory = PeerConnectionFactory.builder()
                .setVideoEncoderFactory(new DefaultVideoEncoderFactory(
                        eglBase.getEglBaseContext(), true, true))
                .setVideoDecoderFactory(new DefaultVideoDecoderFactory(eglBase.getEglBaseContext()))
                .createPeerConnectionFactory();

        // 信令客户端
        signaling = new SignalingClient(signalUrl, userId, new SignalingClient.SignalingListener() {
            @Override
            public void onOffer(String sdp) { handleOffer(sdp); }
            @Override
            public void onAnswer(String sdp) { handleAnswer(sdp); }
            @Override
            public void onCandidate(String sdpMid, int sdpMLineIndex, String candidate) {
                IceCandidate ice = new IceCandidate(sdpMid, sdpMLineIndex, candidate);
                if (pc != null) pc.addIceCandidate(ice);
            }
        });
        signaling.connect();

        createPeerConnection();
        createVideoTrack();
    }

    // 建立 PeerConnection
    private void createPeerConnection() {
        PeerConnection.RTCConfiguration config = new PeerConnection.RTCConfiguration(
                Collections.singletonList(
                        PeerConnection.IceServer.builder("stun:stun.l.google.com:19302").createIceServer()
                )
        );

        pc = factory.createPeerConnection(config, new PeerConnection.Observer() {
            @Override
            public void onSignalingChange(PeerConnection.SignalingState signalingState) {
                // 信令状态变化
            }

            @Override
            public void onIceConnectionChange(PeerConnection.IceConnectionState iceConnectionState) {
                // ICE 连接状态变化
            }

            @Override
            public void onIceConnectionReceivingChange(boolean receiving) {
                // 是否正在接收 ICE 数据
            }

            @Override
            public void onIceGatheringChange(PeerConnection.IceGatheringState iceGatheringState) {
                // ICE 收集状态变化
            }

            @Override
            public void onIceCandidate(IceCandidate iceCandidate) {
                // 新的 ICE candidate，发送到信令服务器
                try {
                    org.json.JSONObject msg = new org.json.JSONObject();
                    msg.put("type", "candidate");
                    msg.put("sdpMid", iceCandidate.sdpMid);
                    msg.put("sdpMLineIndex", iceCandidate.sdpMLineIndex);
                    msg.put("candidate", iceCandidate.sdp);
                    signaling.send(msg);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }

            @Override
            public void onIceCandidatesRemoved(IceCandidate[] iceCandidates) {
                // ICE candidate 被移除
            }

            @Override
            public void onAddStream(MediaStream mediaStream) {
                // 旧 API：远端流加入
            }

            @Override
            public void onRemoveStream(MediaStream mediaStream) {
                // 旧 API：远端流移除
            }

            @Override
            public void onDataChannel(DataChannel dataChannel) {
                // DataChannel 到来
            }

            @Override
            public void onRenegotiationNeeded() {
                // 需要重新协商
            }

            @Override
            public void onAddTrack(RtpReceiver rtpReceiver, MediaStream[] mediaStreams) {
                // 新 API：远端 track 加入
            }

            @Override
            public void onConnectionChange(PeerConnection.PeerConnectionState newState) {
                // WebRTC 新 API：连接整体状态变化
            }
        });
    }

    // 创建 VideoTrack
    private void createVideoTrack() {
        textureHelper = SurfaceTextureHelper.create("WebRTCThread", eglBase.getEglBaseContext());
        videoSource = factory.createVideoSource(false);
        videoTrack = factory.createVideoTrack("video0", videoSource);

        MediaStream stream = factory.createLocalMediaStream("stream0");
        stream.addTrack(videoTrack);
        if (pc != null) pc.addStream(stream);
    }

    // 提供给 MediaCodec 用的 Surface（零拷贝）
    public Surface getInputSurface() {
        return textureHelper.getSurfaceTexture() != null ?
                new Surface(textureHelper.getSurfaceTexture()) : null;
    }

    // 处理远端 offer
    private void handleOffer(String sdp) {
        SessionDescription offer = new SessionDescription(SessionDescription.Type.OFFER, sdp);
        pc.setRemoteDescription(new SimpleSdpObserver(), offer);
        pc.createAnswer(new org.webrtc.SdpObserver() {
            @Override
            public void onCreateSuccess(SessionDescription sessionDescription) {
                pc.setLocalDescription(new SimpleSdpObserver(), sessionDescription);
                try {
                    org.json.JSONObject ans = new org.json.JSONObject();
                    ans.put("type", "answer");
                    ans.put("sdp", sessionDescription.description);
                    signaling.send(ans);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
            @Override public void onSetSuccess() {}
            @Override public void onCreateFailure(String s) {}
            @Override public void onSetFailure(String s) {}
        }, new MediaConstraints());
    }

    // 处理远端 answer
    private void handleAnswer(String sdp) {
        SessionDescription answer = new SessionDescription(SessionDescription.Type.ANSWER, sdp);
        pc.setRemoteDescription(new SimpleSdpObserver(), answer);
    }

    // 停止并释放资源
    public void stop() {
        if (pc != null) pc.close();
        if (videoTrack != null) videoTrack.dispose();
        if (videoSource != null) videoSource.dispose();
        if (textureHelper != null) textureHelper.dispose();
        if (signaling != null) signaling.close();
        if (eglBase != null) eglBase.release();
    }
}