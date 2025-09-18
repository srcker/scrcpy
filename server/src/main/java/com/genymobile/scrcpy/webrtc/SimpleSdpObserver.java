package com.genymobile.scrcpy.webrtc;

// 简化 SDP 回调
import org.webrtc.SdpObserver;
import org.webrtc.SessionDescription;

// 简化 SDP 回调
public class SimpleSdpObserver implements SdpObserver {
    @Override
    public void onCreateSuccess(SessionDescription sessionDescription) {}
    @Override
    public void onSetSuccess() {}
    @Override
    public void onCreateFailure(String s) {}
    @Override
    public void onSetFailure(String s) {}
}