/*
 *  IXSatoriConnection.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2018 Machine Zone. All rights reserved.
 */

#pragma once

#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include <jsoncpp/json/json.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketPerMessageDeflateOptions.h>

namespace ix
{
    enum SatoriConnectionEventType
    {
        SatoriConnection_EventType_Authenticated = 0,
        SatoriConnection_EventType_Error = 1,
        SatoriConnection_EventType_Open = 2,
        SatoriConnection_EventType_Closed = 3
    };

    enum SatoriConnectionPublishMode
    {
        SatoriConnection_PublishMode_Immediate = 0,
        SatoriConnection_PublishMode_Batch = 1
    };

    using SubscriptionCallback = std::function<void(const Json::Value&)>;
    using EventCallback = std::function<void(SatoriConnectionEventType,
                                             const std::string&,
                                             const WebSocketHttpHeaders&)>;
    using TrafficTrackerCallback = std::function<void(size_t size, bool incoming)>;

    class SatoriConnection
    {
    public:
        SatoriConnection();
        ~SatoriConnection();

        /// Configuration / set keys, etc...
        /// All input data but the channel name is encrypted with rc4
        void configure(const std::string& appkey,
                       const std::string& endpoint,
                       const std::string& rolename,
                       const std::string& rolesecret,
                       WebSocketPerMessageDeflateOptions webSocketPerMessageDeflateOptions);

        static void setTrafficTrackerCallback(const TrafficTrackerCallback& callback);

        /// Reset the traffic tracker callback to an no-op one.
        static void resetTrafficTrackerCallback();

        /// Set the closed callback
        void setEventCallback(const EventCallback& eventCallback);

        /// Start the worker thread, used for background publishing
        void start();

        /// Publish a message to a channel
        ///
        /// No-op if the connection is not established
        bool publish(const Json::Value& channels,
                     const Json::Value& msg);

        // Subscribe to a channel, and execute a callback when an incoming
        // message arrives.
        void subscribe(const std::string& channel, SubscriptionCallback cb);

        /// Unsubscribe from a channel
        void unsubscribe(const std::string& channel);

        /// Close the connection
        void disconnect();

        /// Connect to Satori and authenticate the connection
        bool connect();

        /// Returns true only if we're connected
        bool isConnected() const;
        
        /// Flush the publish queue
        bool flushQueue();

        /// Set the publish mode
        void setPublishMode(SatoriConnectionPublishMode publishMode);

        /// Lifecycle management. Free resources when backgrounding
        void suspend();
        void resume();

    private:
        bool sendHandshakeMessage();
        bool handleHandshakeResponse(const Json::Value& data);
        bool sendAuthMessage(const std::string& nonce);
        bool handleSubscriptionData(const Json::Value& pdu);

        void initWebSocketOnMessageCallback();

        bool publishMessage(const std::string& serializedJson);
        void enqueue(const std::string& msg);
        std::string serializeJson(const Json::Value& pdu);

        /// Invoke the traffic tracker callback
        static void invokeTrafficTrackerCallback(size_t size, bool incoming);

        /// Invoke event callbacks
        void invokeEventCallback(SatoriConnectionEventType eventType,
                                 const std::string& errorMsg = std::string(),
                                 const WebSocketHttpHeaders& headers = WebSocketHttpHeaders());
        void invokeErrorCallback(const std::string& errorMsg);

        ///
        /// Member variables
        /// 
        WebSocket _webSocket;

        /// Configuration data
        std::string _appkey;
        std::string _endpoint;
        std::string _role_name;
        std::string _role_secret;
        std::atomic<SatoriConnectionPublishMode> _publishMode;

        // Can be set on control+background thread, protecting with an atomic
        std::atomic<bool> _authenticated;

        // Keep some objects around
        Json::Value _body;
        Json::Value _pdu;
        Json::FastWriter _jsonWriter;
        mutable std::mutex _jsonWriterMutex;

        /// Traffic tracker callback
        static TrafficTrackerCallback _trafficTrackerCallback;

        /// Satori events callbacks
        EventCallback _eventCallback;
        mutable std::mutex _eventCallbackMutex;

        /// Subscription callbacks, only one per channel
        std::unordered_map<std::string, SubscriptionCallback> _cbs;
        mutable std::mutex _cbsMutex;

        // Message Queue can be touched on control+background thread, 
        // protecting with a mutex.
        //
        // Message queue is used when there are problems sending messages so 
        // that sending can be retried later.
        std::deque<std::string> _messageQueue;
        mutable std::mutex _queueMutex;

        // Cap the queue size (100 elems so far -> ~100k)
        static constexpr size_t kQueueMaxSize = 256;
    };
    
} // namespace ix
