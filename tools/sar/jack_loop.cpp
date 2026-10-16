// Finite digital USB fixture, never an ADC/DAC latency measurement.
// The normal ADC source is temporarily disconnected to avoid adding analog
// noise to the exact PCM ramp. Every transient connection is restored on exit.
#include <jack/jack.h>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>
namespace {
jack_client_t *client=nullptr;
std::array<jack_port_t *,8> inputs{},outputs{};
std::atomic<bool> alive{true},steady{false};
std::atomic<unsigned> xruns{0},steady_xruns{0};
volatile std::sig_atomic_t stopped=0;
void signal_stop(int){stopped=1;}
void shutdown(void *){alive=false;}
int xrun(void *){++xruns;if(steady.load())++steady_xruns;return 0;}
int process(jack_nframes_t frames,void *) {
  for(unsigned ch=0;ch<8;++ch) {
    const auto *in=static_cast<const float *>(jack_port_get_buffer(inputs[ch],frames));
    auto *out=static_cast<float *>(jack_port_get_buffer(outputs[ch],frames));
    for(unsigned f=0;f<frames;++f)out[f]=in[f];
  }
  return 0;
}
struct ClientLifetime {~ClientLifetime(){if(client)jack_client_close(client);client=nullptr;}};
struct SavedConnections {
  std::vector<std::pair<std::string,std::string>> rows;
  unsigned errors=0;
  void restore() {
    for(const auto &row:rows) if(alive && jack_port_by_name(client,row.first.c_str()) &&
        jack_port_by_name(client,row.second.c_str()) &&
        !jack_port_connected_to(jack_port_by_name(client,row.second.c_str()),row.first.c_str())) {
      if(jack_connect(client,row.first.c_str(),row.second.c_str()))++errors;
    }
    std::printf("JACK_USB_RESTORE connections=%zu errors=%u\n",rows.size(),errors);rows.clear();std::fflush(stdout);
  }
  ~SavedConnections(){if(!rows.empty())restore();}
};
}
int main(int argc,char **argv) {
  const unsigned seconds=argc==2?unsigned(std::strtoul(argv[1],nullptr,10)):0;
  if(!seconds || seconds>60)return 1;
  std::signal(SIGTERM,signal_stop);std::signal(SIGINT,signal_stop);
  jack_status_t status{};
  client=jack_client_open("rey_sar_check",static_cast<jack_options_t>(JackNoStartServer|JackUseExactName),&status);
  if(!client)return 2;
  ClientLifetime lifetime;SavedConnections saved;
  for(unsigned ch=0;ch<8;++ch) {
    const auto suffix=std::to_string(ch+1);
    inputs[ch]=jack_port_register(client,("in_"+suffix).c_str(),JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);
    outputs[ch]=jack_port_register(client,("out_"+suffix).c_str(),JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
    if(!inputs[ch] || !outputs[ch])return 3;
  }
  jack_set_process_callback(client,process,nullptr);jack_set_xrun_callback(client,xrun,nullptr);jack_on_shutdown(client,shutdown,nullptr);
  if(jack_activate(client))return 4;
  bool ready=false;
  for(unsigned n=0;n<150 && alive && !stopped;++n) {
    if(jack_port_by_name(client,"rey_usb_native_receive:capture_8") && jack_port_by_name(client,"rey_usb_native_send:playback_8")){ready=true;break;}
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if(!ready)return 5;
  for(unsigned ch=0;ch<8;++ch) {
    const auto suffix=std::to_string(ch+1),destination="rey_capture:inject_"+suffix;
    auto *port=jack_port_by_name(client,destination.c_str());if(!port)return 6;
    auto connections=jack_port_get_all_connections(client,port);
    if(connections) {
      for(unsigned n=0;connections[n];++n)saved.rows.emplace_back(connections[n],destination);
      jack_free(connections);
    }
  }
  for(const auto &row:saved.rows)if(jack_disconnect(client,row.first.c_str(),row.second.c_str()))return 6;
  for(unsigned ch=0;ch<8;++ch) {
    const auto suffix=std::to_string(ch+1);
    if(jack_connect(client,("rey_usb_native_receive:capture_"+suffix).c_str(),jack_port_name(inputs[ch])) ||
        jack_connect(client,jack_port_name(outputs[ch]),("rey_usb_native_send:playback_"+suffix).c_str()))return 7;
  }
  std::printf("JACK_USB_CHECK ready; digital_loop=1; isolated_adc_sources=%zu; physical_audio=0\n",saved.rows.size());std::fflush(stdout);
  for(unsigned n=0;n<seconds*10 && alive && !stopped;++n){if(n==50)steady=true;std::this_thread::sleep_for(std::chrono::milliseconds(100));}
  if(alive)jack_deactivate(client);
  saved.restore();
  std::printf("JACK_USB_LOOP xruns=%u steady_xruns=%u startup_seconds=5 physical_audio=0\n",xruns.load(),steady_xruns.load());
  return !alive || steady_xruns || saved.errors?8:0;
}
