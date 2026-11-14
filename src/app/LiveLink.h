#pragma once
// The live link (Kerem, 2026-11-14: the visuals follow the app as it plays, songs and Improvisation alike): what the app
// plays, as server-sent events on http://127.0.0.1:8737/live for the visuals page (visuals/index.html, the Live button).
//   song  a song starting: its show (tempo, key, sections, voices, every note; src/engine showHead) and, for
//         Improvisation, its plan; "at" = when its bar 0 is heard, on the app's clock
//   lv    the seven levels the renders' show.json carries per block (roles, delay, reverb, master), with "at"
//   stop  the playing stopped
// Only this computer can connect (it listens on 127.0.0.1). A page that joins mid-song is sent the songs still sounding.
#include <juce_core/juce_core.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

class LiveLink
{
public:
    static constexpr int kPort = 8737;
    LiveLink() { acceptor.startThread(); sender.startThread(); }
    ~LiveLink()
    {
        acceptor.signalThreadShouldExit();
        server.close(); // unblocks the accept
        sender.signalThreadShouldExit();
        sender.wake.signal();
        acceptor.stopThread(2000);
        sender.stopThread(2000);
    }
    static double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; } // the app's clock, seconds
    // a song begins: kept for pages that join later (the last two: one may still ring out under the next)
    void song(const juce::String& json)
    {
        {
            const std::lock_guard<std::mutex> l(m);
            songs.push_back(json);
            while (songs.size() > 2) songs.pop_front();
        }
        post("song", json);
    }
    void stop() { { const std::lock_guard<std::mutex> l(m); songs.clear(); } post("stop", "{}"); }
    void post(const char* event, const juce::String& json)
    {
        if (clients() == 0) return;
        const std::lock_guard<std::mutex> l(m);
        if (queue.size() > 2000) return; // a page that stopped reading: drop rather than grow
        queue.push_back("event: " + juce::String(event) + "\ndata: " + json + "\n\n");
        sender.wake.signal();
    }
    int clients() const { const std::lock_guard<std::mutex> l(m); return int(socks.size()); }

private:
    struct Acceptor : juce::Thread
    {
        LiveLink& o;
        explicit Acceptor(LiveLink& x) : juce::Thread("live link accept"), o(x) {}
        void run() override
        {
            if (!o.server.createListener(kPort, "127.0.0.1")) return;
            while (!threadShouldExit())
            {
                std::unique_ptr<juce::StreamingSocket> c(o.server.waitForNextConnection());
                if (!c || threadShouldExit()) continue;
                char req[2048] = {};
                if (c->waitUntilReady(true, 1000) == 1) c->read(req, sizeof req - 1, false);
                const juce::String head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
                                          "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Private-Network: true\r\nConnection: keep-alive\r\n\r\n";
                if (!juce::String(req).startsWith("GET /live")) // a CORS preflight or anything else: answer and close
                {
                    const juce::String r = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Private-Network: true\r\n"
                                           "Access-Control-Allow-Methods: GET\r\nAccess-Control-Allow-Headers: *\r\nContent-Length: 0\r\n\r\n";
                    c->write(r.toRawUTF8(), int(r.getNumBytesAsUTF8()));
                    continue;
                }
                c->write(head.toRawUTF8(), int(head.getNumBytesAsUTF8()));
                const std::lock_guard<std::mutex> l(o.m);
                for (auto& s : o.songs) // what is sounding now, so a page that joins mid-song can follow
                {
                    const juce::String ev = "event: song\ndata: " + s + "\n\n";
                    c->write(ev.toRawUTF8(), int(ev.getNumBytesAsUTF8()));
                }
                o.socks.push_back(std::move(c));
            }
        }
    };
    struct Sender : juce::Thread
    {
        LiveLink& o;
        juce::WaitableEvent wake;
        explicit Sender(LiveLink& x) : juce::Thread("live link send"), o(x) {}
        void run() override
        {
            while (!threadShouldExit())
            {
                wake.wait(100);
                std::deque<juce::String> q;
                std::vector<juce::StreamingSocket*> to;
                {
                    const std::lock_guard<std::mutex> l(o.m);
                    q.swap(o.queue);
                    for (auto& s : o.socks) to.push_back(s.get());
                }
                std::vector<juce::StreamingSocket*> dead;
                for (auto* s : to)
                    for (auto& msg : q)
                        if (s->write(msg.toRawUTF8(), int(msg.getNumBytesAsUTF8())) < 0) { dead.push_back(s); break; }
                if (!dead.empty())
                {
                    const std::lock_guard<std::mutex> l(o.m);
                    for (auto* d : dead)
                        o.socks.erase(std::remove_if(o.socks.begin(), o.socks.end(), [d](auto& p) { return p.get() == d; }), o.socks.end());
                }
            }
        }
    };
    mutable std::mutex m;
    juce::StreamingSocket server;
    std::vector<std::unique_ptr<juce::StreamingSocket>> socks;
    std::deque<juce::String> songs, queue;
    Acceptor acceptor { *this };
    Sender sender { *this };
};
