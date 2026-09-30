  // RX, ASIO processing and TX each own their queues/cursors. No socket call
  // or unbounded historical catch-up runs inside a host callback.
  struct Counters {
#define AOIP_COUNTERS(X) \
    X(rx_packets) X(invalid_packets) X(rx_queue_overflows) X(late_frames) \
    X(missing_frames) X(resyncs) X(callbacks) X(deadline_misses) X(skipped_frames) \
    X(max_callback_ns) X(max_wake_late_ns) X(max_rx_gap_ns) X(tx_packets) \
    X(tx_queue_overflows) X(tx_expired_packets) X(tx_errors) X(mmcss_failures) X(last_tx_error) X(host_overruns) X(expired_output_frames) X(tx_retries)
#define DECLARE(name) std::atomic<uint64_t> name{0};
    AOIP_COUNTERS(DECLARE)
#undef DECLARE
    void reset() {
#define RESET(name) name=0;
      AOIP_COUNTERS(RESET)
#undef RESET
    }
    void snapshot(aoip::Diagnostics& out) const {
#define COPY(name) out.name=name.load(std::memory_order_relaxed);
      AOIP_COUNTERS(COPY)
#undef COPY
    }
#undef AOIP_COUNTERS
  } stats_;

  void receive_loop() {
    RealtimeThread realtime(stats_.mmcss_failures,1,cfg_.realtime_cpus[1]);
    rx_trace_tid_=GetCurrentThreadId();
    SetThreadDescription(GetCurrentThread(),L"PiAoIP receive");
    if(source_v3_) {
      // A quiet source sends only its three transition notices. Leave them in
      // the socket until subscribe has supplied the epoch, rather than dropping
      // all initial notices while the control acknowledgement is in flight.
      HANDLE start_events[]={stop_event_,start_event_};
      if(WaitForMultipleObjects(2,start_events,FALSE,INFINITE)!=WAIT_OBJECT_0+1) { rx_cpu_ns_=thread_cpu_ns(); return; }
    }
    aoip::Packet packet;
    uint64_t previous=0;
    uint64_t wait_begin=0,wait_end=0;
    unsigned fast_after_gap=0;
    HANDLE events[]={stop_event_,socket_event_};
    while(running_.load(std::memory_order_relaxed)) {
      bool received=false;
      unsigned batch_count=0;
      for(unsigned batch=0; batch<256 && running_; ++batch) {
        const uint64_t recv_begin=timing_trace_enabled_ ? now_ns() : 0;
        int n=recv(socket_,reinterpret_cast<char*>(packet.data),sizeof(packet.data),0);
        if(n==SOCKET_ERROR) {
          int error=WSAGetLastError();
          if(error!=WSAEWOULDBLOCK && error!=WSAECONNRESET) ++stats_.invalid_packets;
          break;
        }
        packet.time_ns=now_ns(); packet.size=unsigned(n);
        if(!aoip::validate_packet(packet.data,packet.size,1,cfg_.channels,cfg_.rate,cfg_.bits) ||
          (source_v3_ && (aoip::get16(packet.data+4)!=3 || (aoip::get64(packet.data+40) & ~input_map_.mask)))) {
          ++stats_.invalid_packets; continue;
        }
        if(source_v3_ && aoip::get16(packet.data+34)!=expected_rx_epoch_.load()) continue;
        const bool silent=source_v3_ && aoip::get64(packet.data+40)==0;
        if(previous && !silent) {
          const uint64_t gap=packet.time_ns-previous;
          max_counter(stats_.max_rx_gap_ns,gap);
          if(timing_trace_enabled_) {
            if(gap>=250000) {
              if(rx_gap_record_count_<rx_gap_records_.size())
                rx_gap_records_[rx_gap_record_count_++]={previous,packet.time_ns,recv_begin,wait_begin,wait_end,
                  aoip::get32(packet.data+12),GetCurrentProcessorNumber()};
              else ++rx_gap_records_dropped_;
            }
            // 5 us buckets preserve tail information without logging on the
            // real-time receive thread. The final bucket also covers >2.55 ms.
            ++rx_gap_hist_[std::min<uint64_t>(gap/5000,rx_gap_hist_.size()-1)];
            if(gap>=500000) ++rx_gap_over_500us_;
            if(gap>=1000000) { ++rx_gap_over_1000us_; fast_after_gap=1; }
            else if(fast_after_gap && gap<=20000) {
              ++fast_after_gap; max_counter(rx_fast_after_gap_,fast_after_gap);
            } else fast_after_gap=0;
            if(gap>=1500000) ++rx_gap_over_1500us_;
          }
        }
        previous=silent ? 0 : packet.time_ns;
        ++stats_.rx_packets;
        if(!rx_queue_->push(packet)) ++stats_.rx_queue_overflows;
        else if(timing_trace_enabled_) max_counter(rx_max_queue_depth_,rx_queue_->size());
        ++batch_count;
        received=true;
      }
      if(timing_trace_enabled_) max_counter(rx_max_batch_,batch_count);
      if(received) { SetEvent(rx_event_); continue; }
      if(timing_trace_enabled_) wait_begin=now_ns();
      const auto wait_result=WaitForMultipleObjects(2,events,FALSE,100);
      if(timing_trace_enabled_) wait_end=now_ns();
      if(wait_result==WAIT_OBJECT_0) break;
      WSANETWORKEVENTS network{};
      WSAEnumNetworkEvents(socket_,socket_event_,&network);
    }
    rx_cpu_ns_=thread_cpu_ns();
  }

  void unsubscribe_source() {
    if(!source_v3_ || !session_) return;
    char request[96]{},reply[512]{}; sockaddr_in responder{};
    std::snprintf(request,sizeof(request),"PIAOIP_UNSUBSCRIBE_V3 session=%u",session_);
    for(unsigned attempt=0;attempt<2;++attempt) if(control_request(cfg_.peer,nullptr,request,reply,responder,8)) break;
    session_=0;
  }
  void heartbeat_loop() {
    unsigned failures=0;
    HANDLE events[]={stop_event_,config_event_};
    while(running_) {
      const auto result=WaitForMultipleObjects(2,events,FALSE,1000);
      if(result==WAIT_OBJECT_0) break;
      if(result==WAIT_OBJECT_0+1) {
        unsubscribe_source(); running_=false; SetEvent(stop_event_);
        auto callback=reset_callback_.load();
        if(callback) {
          AddRef();
          try { std::thread([this,callback] { if(reset_callback_.load()==callback) callback(kAsioResetRequest,0,nullptr,nullptr); Release(); }).detach(); }
          catch(...) { Release(); ++control_failures_; }
        }
        break;
      }
      if(result!=WAIT_TIMEOUT || !source_v3_) continue;
      char request[96]{},reply[512]{}; sockaddr_in responder{};
      std::snprintf(request,sizeof(request),"PIAOIP_KEEPALIVE_V3 session=%u",session_);
      unsigned session=0,source_silent=0,sink_silent=0; int end=0;
      bool okay=control_request(cfg_.peer,nullptr,request,reply,responder,10) &&
        std::sscanf(reply,"PIAOIP_ALIVE_V3 session=%u source_silent=%u sink_silent=%u%n",&session,&source_silent,&sink_silent,&end)==3 &&
        end==int(std::strlen(reply)) && session==session_;
      if(okay) failures=0;
      else { ++control_failures_; if(++failures>=2) { running_=false; SetEvent(stop_event_); break; } }
    }
  }

  void send_loop() {
    RealtimeThread realtime(stats_.mmcss_failures,2,cfg_.realtime_cpus[2]);
    tx_trace_tid_=GetCurrentThreadId();
    SetThreadDescription(GetCurrentThread(),L"PiAoIP transmit");
    DeadlineWaiter waiter;
    aoip::Packet packet;
    HANDLE events[]={stop_event_,tx_event_};
    // This is a discard bound, not a wait. Independent TX scheduling may stall
    // briefly after audio has rendered; allow a measured 3 ms scheduling grace.
    const uint64_t expiry=std::max<uint64_t>(3000000,uint64_t(block_+cfg_.safety)*1000000000/cfg_.rate);
    while(running_.load(std::memory_order_relaxed)) {
      if(!tx_queue_->pop(packet)) { WaitForMultipleObjects(2,events,FALSE,100); continue; }
      const auto age=now_ns(); if(age>packet.time_ns) max_counter(max_tx_queue_age_ns_,age-packet.time_ns);
      if(now_ns()>packet.time_ns+expiry) { ++stats_.tx_expired_packets; continue; }
      waiter.wait(packet.time_ns,stop_event_);
      if(!running_) break;
      if(now_ns()>packet.time_ns+expiry) { ++stats_.tx_expired_packets; continue; }
      while(running_) {
        if(now_ns()>packet.time_ns+expiry) { ++stats_.tx_expired_packets; break; }
        int n=send(socket_,reinterpret_cast<const char*>(packet.data),int(packet.size),0);
        if(n==int(packet.size)) { ++stats_.tx_packets; break; }
        const int error=WSAGetLastError();
        if(n==SOCKET_ERROR && error==WSAEWOULDBLOCK) {
          // Transient backpressure is retried only until the original deadline.
          // The audio thread remains independent and the retry is observable.
          ++stats_.tx_retries;
          waiter.wait(now_ns()+25000,stop_event_);
          continue;
        }
        ++stats_.tx_errors; stats_.last_tx_error=error; break;
      }
    }
    tx_cpu_ns_=thread_cpu_ns();
  }

  unsigned output_packet_frames() const {
    // Small channel counts must not wait milliseconds to fill the MTU.
    // Aim for <=125 us of audio, also bounded by one host block and the MTU.
    return aoip::low_latency_frames(wire_outputs_,cfg_.rate,cfg_.bits,unsigned(block_));
  }

  void enqueue_output(int half,uint64_t now) {
    if(!wire_outputs_) return;
    if(source_v3_) {
      uint64_t signal=cfg_.energy_saving ? 0 : output_map_.mask;
      const unsigned asio_bytes=cfg_.bits==16 ? 2 : 4;
      if(cfg_.energy_saving) for(auto& channel:buffers_) if(!channel.input && channel.enabled && channel.packed>=0) {
        const auto* pcm=channel.halves[half].get();
        bool nonzero=false;
        for(long f=0;f<block_ && !nonzero;++f) for(unsigned b=0;b<cfg_.bits/8;++b)
          nonzero=nonzero || pcm[size_t(f)*asio_bytes+b]!=0;
        if(nonzero) signal |= uint64_t(1)<<channel.channel;
      }
      output_signal_mask_=signal;
      if(!signal) {
        ++silent_output_blocks_; suppressed_output_frames_+=block_;
        if(output_silent_) { tx_frame_+=block_; return; }
      }
      aoip::ChannelMap wire; wire.assign(signal);
      // The complete host block is ready. Fragment only at the MTU, without
      // adding a packetization delay or waking TX for unnecessary fragments.
      const unsigned limit=std::min(aoip::masked_packet_frames(wire.count,cfg_.bits),unsigned(block_));
      unsigned offset=0;
      do {
        const unsigned frames=signal ? std::min(limit,unsigned(block_)-offset) : std::min(256u,unsigned(block_));
        aoip::Packet packet; packet.time_ns=now; // this block is already rendered; send it immediately
        aoip::write_masked_header(packet.data,2,tx_seq_++,tx_frame_+offset,signal,cfg_.rate,cfg_.bits,frames,tx_epoch_);
        const unsigned stride=wire.count*(cfg_.bits/8);
        for(auto& channel:buffers_) if(!channel.input && channel.enabled && wire.packed[unsigned(channel.channel)]>=0) {
          const unsigned packed=unsigned(wire.packed[unsigned(channel.channel)]);
          for(unsigned f=0;f<frames;++f)
            std::memcpy(packet.data+aoip::masked_header_bytes+size_t(f)*stride+packed*(cfg_.bits/8),
              channel.halves[half].get()+size_t(offset+f)*asio_bytes,cfg_.bits/8);
        }
        packet.size=aoip::masked_header_bytes+frames*stride;
        aoip::put32(packet.data+36,aoip::packet_crc(packet.data,packet.size));
        // Repeat the silence transition three times, then send no audio during
        // digital zero. PCM resumes in the first nonzero block, with its frame tag.
        const unsigned copies=signal ? 1 : 3;
        for(unsigned copy=0;copy<copies;++copy) {
          if(copy) { aoip::put32(packet.data+12,tx_seq_++); aoip::put32(packet.data+36,aoip::packet_crc(packet.data,packet.size)); }
          if(!tx_queue_->push(packet)) ++stats_.tx_queue_overflows;
        }
        offset=signal ? offset+frames : unsigned(block_);
      } while(offset<unsigned(block_));
      SetEvent(tx_event_);
      tx_frame_+=block_; output_silent_=!signal;
      return;
    }
    const unsigned stride=wire_outputs_*(cfg_.bits/8);
    const unsigned packet_frames=output_packet_frames();
    const unsigned asio_bytes=cfg_.bits==16 ? 2 : 4;
    for(long f=0; f<block_; ++f) {
      if(!partial_frames_) {
        partial_.time_ns=now+uint64_t(f)*1000000000/cfg_.rate;
        aoip::write_header(partial_.data,2,tx_seq_++,tx_frame_,wire_outputs_,cfg_.rate,cfg_.bits,packet_frames);
        if(source_v2_) { aoip::put16(partial_.data+4,2); aoip::put16(partial_.data+34,tx_epoch_); }
      }
      auto* target=partial_.data+aoip::header_bytes+size_t(partial_frames_)*stride;
      std::memset(target,0,stride);
      for(auto& channel:buffers_) if(!channel.input && channel.enabled)
        std::memcpy(target+channel.channel*(cfg_.bits/8),channel.halves[half].get()+size_t(f)*asio_bytes,cfg_.bits/8);
      ++tx_frame_;
      if(++partial_frames_==packet_frames) {
        partial_.size=aoip::header_bytes+packet_frames*stride;
        aoip::put32(partial_.data+36,aoip::packet_crc(partial_.data,partial_.size));
        if(!tx_queue_->push(partial_)) ++stats_.tx_queue_overflows;
        else SetEvent(tx_event_);
        partial_frames_=0;
      }
    }
  }

  uint64_t allowed_lateness() const {
    // Recover short scheduling stalls using the explicitly selected guard.
    // Never replay an unbounded backlog when the host actually hangs.
    return std::max<uint64_t>(500000,
      uint64_t(std::max(2u*unsigned(block_),cfg_.safety))*1000000000/cfg_.rate);
  }

  void deliver(uint64_t scheduled) {
    // If the host has no outputReady handshake, use the normal ASIO one-cycle
    // output pipeline. Never send a half immediately after requesting deferred work.
    if(pending_half_>=0) { enqueue_output(pending_half_,now_ns()); pending_half_=-1; }
    unsigned signal=input_map_.count && !remote_silent_ ? unsigned(block_) : 0;
    if(silence_first_!=UINT64_MAX) signal=timeline_.cursor>=silence_first_ ? 0 :
      unsigned(std::min<uint64_t>(signal,silence_first_-timeline_.cursor));
    const unsigned missing=signal ? timeline_.read(input_scratch_.data(),signal) : 0;
    if(signal<unsigned(block_)) {
      const auto width=std::max(1u,input_map_.count);
      std::fill(input_scratch_.begin()+size_t(signal)*width,input_scratch_.begin()+size_t(block_)*width,0);
      timeline_.cursor+=unsigned(block_)-signal;
    }
    if(silence_first_!=UINT64_MAX && timeline_.cursor>=silence_first_) remote_silent_=true;
    stats_.missing_frames+=missing;
    for(auto& channel:buffers_) {
      if(!channel.input) continue;
      for(long f=0; f<block_; ++f) {
        int32_t value=channel.enabled && channel.packed>=0 ? input_scratch_[size_t(f)*input_map_.count+unsigned(channel.packed)] : 0;
        if(cfg_.bits==16) reinterpret_cast<int16_t*>(channel.halves[half_].get())[f]=int16_t(value);
        else reinterpret_cast<int32_t*>(channel.halves[half_].get())[f]=value;
      }
    }
    timestamp_=scheduled;
    position_=stream_position_;
    output_ready_=false;
    const uint64_t before=now_ns();
    uint64_t first=0; first_callback_ns_.compare_exchange_strong(first,before-start_requested_ns_);
    if(time_info_) {
      ASIOTime time{}; const auto pos=position_.load();
      time.timeInfo.samplePosition.lo=uint32_t(pos); time.timeInfo.samplePosition.hi=uint32_t(pos>>32);
      time.timeInfo.systemTime.lo=uint32_t(scheduled); time.timeInfo.systemTime.hi=uint32_t(scheduled>>32);
      time.timeInfo.sampleRate=cfg_.rate; time.timeInfo.speed=1.0;
      time.timeInfo.flags=kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
      callbacks_->bufferSwitchTimeInfo(&time,half_,ASIOTrue);
    } else callbacks_->bufferSwitch(half_,ASIOTrue);
    const auto finished=now_ns();
    last_callback_ns_=finished-before;
    max_counter(stats_.max_callback_ns,finished-before);
    const auto period=uint64_t(block_)*1000000000/cfg_.rate;
    if(finished-before>period) ++stats_.host_overruns;
    ++stats_.callbacks;
    if(finished>scheduled+allowed_lateness()) {
      stats_.expired_output_frames+=block_; tx_frame_+=block_;
      partial_frames_=0; pending_half_=-1;
    } else if(output_ready_.load(std::memory_order_acquire)) enqueue_output(half_,finished);
    else pending_half_=half_;
    stream_position_+=block_;
    half_^=1;
  }

  void audio_loop() {
    audio_thread_id_=GetCurrentThreadId();
    audio_trace_tid_=GetCurrentThreadId();
    SetThreadDescription(GetCurrentThread(),L"PiAoIP audio");
    RealtimeThread realtime(stats_.mmcss_failures,0,cfg_.realtime_cpus[0]);
    DeadlineWaiter waiter;
    HANDLE start_events[]={stop_event_,start_event_};
    if(WaitForMultipleObjects(2,start_events,FALSE,INFINITE)!=WAIT_OBJECT_0+1) { audio_thread_id_=0; return; }
    aoip::Packet packet;
    const double nominal=double(block_)*1000000000.0/cfg_.rate;
    double deadline=double(now_ns())+nominal;
    uint64_t last_packet_ns=0;
    bool locked=source_v3_ && input_map_.count==0;
    uint16_t receive_epoch=0;
    while(running_.load(std::memory_order_relaxed)) {
      // Bound work even if a faulty sender floods the socket. A queue holds
      // <=1024 packets; old frames are discarded using the source timeline.
      for(unsigned batch=0; batch<1024 && rx_queue_->pop(packet); ++batch) {
        if(timing_trace_enabled_) {
          const uint64_t read_at=now_ns();
          if(read_at>packet.time_ns) max_counter(audio_max_packet_age_ns_,read_at-packet.time_ns);
        }
        const auto first=aoip::get64(packet.data+16);
        const bool v2=aoip::get16(packet.data+4)>=2;
        const bool silence=source_v3_ && aoip::get64(packet.data+40)==0;
        const uint16_t epoch=aoip::get16(packet.data+34);
        if(locked && !silence && remote_silent_ && silence_first_!=UINT64_MAX && first<silence_first_) {
          stats_.late_frames+=aoip::get16(packet.data+30); continue;
        }
        const bool restart=locked && (v2 ? epoch!=receive_epoch :
          (first<timeline_.cursor && timeline_.cursor-first>cfg_.rate/4));
        const bool reacquire=locked && !silence && (remote_silent_.load() || (last_packet_ns && packet.time_ns-last_packet_ns>250000000));
        const bool far_ahead=locked && first>timeline_.cursor && first-timeline_.cursor>cfg_.rate/4;
        if(!locked || restart || reacquire || far_ahead) {
          if(locked) ++stats_.resyncs;
          timeline_.reset(first); locked=true;
          silence_first_=UINT64_MAX; remote_silent_=false;
          receive_epoch=epoch;
          // One block plus an explicit, small guard; host buffering is independent
          // of the MTU-sized wire packets.
          deadline=double(packet.time_ns)+double(block_+cfg_.safety)*1000000000.0/cfg_.rate;
        }
        last_packet_ns=packet.time_ns;
        if(silence) {
          if(first<timeline_.latest) {
            // A delayed marker must not mute newer PCM already in the timeline.
            timeline_.insert_silence(first,first+aoip::get16(packet.data+30));
          } else silence_first_=std::min(silence_first_,first);
        } else {
          if(silence_first_!=UINT64_MAX && first>=silence_first_) {
            // Short quiet intervals can resume before their buffered boundary is
            // played. Tag the intentional gap and keep the original clock.
            timeline_.insert_silence(silence_first_,first); silence_first_=UINT64_MAX;
          }
          remote_silent_=false;
          stats_.late_frames+=timeline_.insert(packet.data,source_v3_ ? &input_map_ : nullptr);
        }
      }
      if(!locked) {
        HANDLE events[]={stop_event_,rx_event_};
        WaitForMultipleObjects(2,events,FALSE,20);
        continue;
      }
      const auto now=now_ns();
      if(double(now)<deadline) {
        const bool idle=cfg_.energy_saving && (!input_map_.count || remote_silent_) && output_silent_;
        // Network arrivals provide an early wake for the Pi clock; Windows
        // timers alone may sleep longer than a small ASIO period. The tail spin
        // is bounded, and silent/local-clock operation uses the timer only.
        // Reduce the active tail when the host uses more of its period. A larger
        // configured tail must not turn a busy DAW callback into a full-core spin.
        const auto active_budget=uint64_t(nominal*0.70);
        const auto remaining=last_callback_ns_<active_budget ? active_budget-last_callback_ns_ : 0;
        waiter.wait(uint64_t(deadline),stop_event_,input_map_.count && !remote_silent_ ? rx_event_ : nullptr,
          idle ? 0 : std::min({active_spin_ns_,uint64_t(nominal/2),remaining})); continue;
      }
      const uint64_t late=now-uint64_t(deadline);
      if(timing_trace_enabled_ && late>=250000) {
        if(audio_late_record_count_<audio_late_records_.size())
          audio_late_records_[audio_late_record_count_++]={uint64_t(deadline),now,GetCurrentProcessorNumber()};
        else ++audio_late_records_dropped_;
      }
      max_counter(stats_.max_wake_late_ns,late);
      if(late>=allowed_lateness()) {
        const uint64_t skipped=uint64_t(double(late)/nominal);
        ++stats_.deadline_misses;
        stats_.skipped_frames+=skipped*block_;
        timeline_.cursor+=skipped*block_;
        stream_position_+=skipped*block_;
        tx_frame_+=skipped*block_;
        partial_frames_=0;
        pending_half_=-1; // expired host audio must not be returned after recovery
        deadline+=double(skipped)*nominal;
      }
      const int64_t queued=int64_t(timeline_.latest)-int64_t(timeline_.cursor);
      const int64_t target=block_+cfg_.safety;
      if(input_map_.count && !remote_silent_ && queued>target+std::max<int64_t>({2*block_,cfg_.rate/1000,cfg_.safety})) {
        const auto skip=uint64_t((queued-target)/block_)*block_;
        timeline_.cursor+=skip; stats_.skipped_frames+=skip; ++stats_.resyncs;
      }
      // Bounded clock discipline follows the Pi's average sample cadence.
      // +/-2000 ppm, with half-second correction, cannot chase a packet burst.
      const double error=input_map_.count && !remote_silent_ ? double(int64_t(timeline_.latest)-int64_t(timeline_.cursor)-target) : 0.0;
      const double correction=std::clamp(error/(cfg_.rate*0.5),-0.002,0.002);
      deliver(uint64_t(deadline));
      deadline+=nominal*(1.0-correction);
    }
    audio_thread_id_=0;
    audio_cpu_ns_=thread_cpu_ns();
  }

  std::atomic<ULONG> refs_{1};
  Config cfg_;
  SOCKET socket_=INVALID_SOCKET;
  WSAEVENT socket_event_=WSA_INVALID_EVENT;
  HANDLE stop_event_=nullptr,rx_event_=nullptr,tx_event_=nullptr,start_event_=nullptr,config_event_=nullptr;
  bool wsa_=false, time_info_=false;
  uint64_t active_spin_ns_=20000;
  uint64_t last_callback_ns_=0; // owned by the audio thread, reset before start
  char error_[128]{};
  std::atomic<bool> running_{false}, output_ready_{false};
  std::atomic<DWORD> audio_thread_id_{0};
  std::thread receiver_,worker_,sender_,heartbeat_;
  aoip::ChannelMap input_map_,output_map_;
  uint32_t session_=0;
  std::atomic<uint16_t> expected_rx_epoch_{0};
  std::atomic<bool> remote_silent_{false};
  bool output_silent_=false;
  uint64_t start_requested_ns_=0;
  std::atomic<uint64_t> first_callback_ns_{0},silent_output_blocks_{0},suppressed_output_frames_{0},output_signal_mask_{0},control_failures_{0};
  std::atomic<uint64_t> audio_cpu_ns_{0},rx_cpu_ns_{0},tx_cpu_ns_{0};
  std::atomic<uint64_t> max_tx_queue_age_ns_{0};
  bool timing_trace_enabled_=false,timing_trace_printed_=false;
  // Single-writer fixed storage; formatted only after all audio threads join.
  // Absolute QPC-derived ns lets ETW correlate gaps without real-time logging.
  struct RxGapRecord { uint64_t previous,end,recv_begin,wait_begin,wait_end; uint32_t sequence,cpu; };
  struct AudioLateRecord { uint64_t deadline,actual; uint32_t cpu; };
  std::array<RxGapRecord,4096> rx_gap_records_{};
  std::array<AudioLateRecord,4096> audio_late_records_{};
  size_t rx_gap_record_count_=0,audio_late_record_count_=0;
  uint64_t rx_gap_records_dropped_=0,audio_late_records_dropped_=0;
  DWORD rx_trace_tid_=0,tx_trace_tid_=0,audio_trace_tid_=0;
  std::atomic<uint64_t> rx_gap_over_500us_{0},rx_gap_over_1000us_{0},rx_gap_over_1500us_{0};
  std::atomic<uint64_t> rx_max_batch_{0},rx_fast_after_gap_{0},rx_max_queue_depth_{0};
  std::array<std::atomic<uint64_t>,512> rx_gap_hist_{};
  std::atomic<uint64_t> audio_max_packet_age_ns_{0};
  ASIOCallbacks* callbacks_=nullptr;
  using ResetCallback=long (*)(long,long,void*,double*);
  std::atomic<ResetCallback> reset_callback_{nullptr};
  std::vector<ChannelBuffer> buffers_;
  std::unique_ptr<aoip::SpscQueue<aoip::Packet,1024>> rx_queue_,tx_queue_;
  aoip::Timeline timeline_;
  std::vector<int32_t> input_scratch_;
  std::atomic<uint64_t> position_{0},timestamp_{0};
  uint64_t tx_frame_=0;
  uint64_t silence_first_=UINT64_MAX; // frame boundary, owned by audio thread
  uint64_t stream_position_=0;
  uint32_t tx_seq_=0;
  uint16_t tx_epoch_=1;
  unsigned wire_outputs_=0,physical_outputs_=0,source_frames_=1,partial_frames_=0;
  bool source_v2_=false,source_v3_=false;
  aoip::Packet partial_;
  long block_=32;
  int half_=0,pending_half_=-1;
