#include <cstdlib>
#if defined(USE_EXPERIMENTAL_FS)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#if defined(__APPLE__)
#include <unistd.h>
#endif
#endif
#include <cstdint>
#include <iomanip>
#include "CRSDK/CameraRemote_SDK.h"
#include "CameraDevice.h"
#include "Text.h"

#include <chrono> 
using namespace std::chrono; 

#define height 680
#define width  1024
#define JPEG_SIZE 305676
#define MAX_SEND_PACK 8192
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib-unix.h>
#include <signal.h>

// Include gstreamer library
#include <gst/gst.h>
#include <gst/app/app.h>
#include <queue>
#include <unistd.h>

#include <thread>
#include <glib-unix.h>
#include <signal.h>
#include <chrono>
//#define LIVEVIEW_ENB
#include <fstream>

namespace SDK = SCRSDK;

// Global dll object
// cli::CRLibInterface* cr_lib = nullptr;
typedef std::shared_ptr<cli::CameraDevice> CameraDevicePtr;
/* gst local decleration */
std::queue<GstMapInfo> m_stream_sample;
std::queue<GstBuffer> m_stream_buffer;

CameraDevicePtr camera;
bool camera_ready = false;
GstElement*          pipeline;
GMainLoop*           loop = nullptr;
GstElement *appsrc;
GstElement *sink;
struct sockaddr_in addr;
int addr_len = sizeof(struct sockaddr_in);
void gstreamer_init(char* host_addr, int host_port);
void feed_data_to_gstreamer(char* data, uint32_t size);
void check_frame(uint8_t* _data, int _len);
uint8_t get_nxt_sample(GstMapInfo& _sample);

// thread
pthread_t thrd_send_buffer, thrd_push_buffer, thrd_get_buffer;
bool thrd_must_exit = false;
bool all_threads_init();
void *send_buffer_handle(void *threadid);
void *push_buffer_handle(void *threadid);
void *get_buffer_handle(void *threadid);
bool m_is_frame_feed = false;
bool is_need_data = false;
bool m_pipeline_ready = false;
int stream_port = 5004;
const char* stream_ip = "192.168.140.3";
int s;

static void sigint_handler(int data) {
    // Quit the main loop on SIGINT.
    if(loop != nullptr)
        g_main_loop_quit(loop);

    printf("\n");
    printf("TERMINATING AT USER REQUEST\n");
    printf("\n");

    thrd_must_exit = true;
    if(thrd_send_buffer != NULL)
        pthread_join(thrd_send_buffer, NULL);
    if(thrd_push_buffer != NULL)
        pthread_join(thrd_push_buffer, NULL);
    if(thrd_get_buffer != NULL)
        pthread_join(thrd_get_buffer, NULL);

    // port
    try {
        
    }
    catch (int error){}

    // end program here
    exit(0);
}

static void
cb_need_data(GstElement *appsrc,
             guint unused_size,
             gpointer user_data)
{
    #if 1
    // printf("cb_need_data\n");
    // printf("the size buffer of appsrc need: [%d]\n",unused_size);
    is_need_data = true;
    #else
     g_print("In %s\n", __func__);
    static gboolean white = FALSE;
    static GstClockTime timestamp = 0;
    GstBuffer *buffer;
    guint size;
    GstFlowReturn ret;

    size = 385 * 288 * 2;

    buffer = gst_buffer_new_allocate (NULL, size, NULL);

    /* this makes the image black/white */
    gst_buffer_memset (buffer, 0, white ? 0xff : 0x0, size);

    white = !white;

    GST_BUFFER_PTS (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 2);

    timestamp += GST_BUFFER_DURATION (buffer);

    //g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
    ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);

    if (ret != GST_FLOW_OK) {
        /* something wrong, stop pushing */
        g_main_loop_quit (loop);
    }
    #endif
}

static void cb_enough_data(GstAppSrc *src, gpointer user_data)
{
    g_print("-------------------------------------------------In %s\n", __func__);
}
/**
 * @brief Check preroll to get a new frame using callback
 *  https://gstreamer.freedesktop.org/documentation/design/preroll.html
 * @return GstFlowReturn
 */
GstFlowReturn new_preroll(GstAppSink* /*appsink*/, gpointer /*data*/)
{
    cli::tout << "preroll exist\n" ;
    return GST_FLOW_OK;
}

/**
 * @brief This is a callback that get a new frame when a preroll exist
 *
 * @param appsink
 * @return GstFlowReturn
 */

auto start =high_resolution_clock::now();
auto stop = high_resolution_clock::now(); 
auto duration = high_resolution_clock::now(); 
int cnt = 0;


GstFlowReturn new_sample(GstAppSink* appsink, gpointer /*data*/)
{
    // Get caps and frame
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    //GstStructure *structure = gst_caps_get_structure(caps, 0);
    //const int width = g_value_get_int(gst_structure_get_value(structure, "width"));
    //const int height = g_value_get_int(gst_structure_get_value(structure, "height"));
 
    // Get frame data
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);

 #if 0
    fill_buffer((uint8_t* )map.data, map.size);
 #else
    // add sample data to queue
    m_stream_sample.push(map);
    m_is_frame_feed = true;
    cnt++;
    // printf(".");
    if(cnt == 1){
        start = high_resolution_clock::now(); 
    }
    if(cnt == 31){
        auto stop = high_resolution_clock::now(); 
        auto duration = duration_cast<microseconds>(stop - start); 
        cnt=0;
        // printf("30frames time: %d\n", duration.count());
    }
    // printf("queue size: %d\n", m_stream_sample.size());
 #endif

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
 
    return GST_FLOW_OK;
}

/**
 * @brief Bus callback
 *  Print important messages
 *
 * @param bus
 * @param message
 * @param data
 * @return gboolean
 */

static gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data)
{
    // Debug message
    //g_print("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    switch(GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
 
            gst_message_parse_error(message, &err, &debug);
            g_print("------------------------------------Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS:
            /* end-of-stream */
            g_print("end-of-stream\n");
            break;
        default:
            /* unhandled message */
            break;
    }
    /* we want to be notified again the next time there is a message
     * on the bus, so returning TRUE (FALSE means we want to stop watching
     * for messages on the bus and our callback should not be called again)
     */
    return true;
}


static void reboot_system(){
    // int32_t ret;
    // char cmdStr[100];

    // memset(cmdStr, 0, sizeof(cmdStr));

    // snprintf(cmdStr, sizeof(cmdStr), "/sbin/reboot");
    // ret = system(cmdStr);
    // if (ret < 0) {
    //     cli::tout << "Can't reboot system.\n";
    // }else
    //     cli::tout << "System rebooting...\n";
}

CameraDevicePtr camera_init()
{
    // Change global locale to native locale
    std::locale::global(std::locale(""));

    // Make the stream's locale the same as the current global locale
    cli::tin.imbue(std::locale());
    cli::tout.imbue(std::locale());

    cli::tout << "RemoteSampleApp v1.04.00 running...\n\n";

    CrInt32u version = SDK::GetSDKVersion();
    int major = (version & 0xFF000000) >> 24;
    int minor = (version & 0x00FF0000) >> 16;
    int patch = (version & 0x0000FF00) >> 8;
    // int reserved = (version & 0x000000FF);

    cli::tout << "Remote SDK version: ";
    cli::tout << major << "." << minor << "." << std::setfill(TEXT('0')) << std::setw(2) << patch << "\n";

    // Load the library dynamically
    // cr_lib = cli::load_cr_lib();

    cli::tout << "Initialize Remote SDK...\n";

#if defined(__APPLE__)
    char path[255]; /*MAX_PATH*/
    getcwd(path, sizeof(path) - 1);
    cli::tout << "Working directory: " << path << '\n';
#else
    cli::tout << "Working directory: " << fs::current_path() << '\n';
#endif
    // auto init_success = cr_lib->Init(0);
    auto init_success = SDK::Init();
    if (!init_success)
    {
        cli::tout << "Failed to initialize Remote SDK. Terminating.\n";
        // cr_lib->Release();
        SDK::Release();
        std::exit(EXIT_FAILURE);
    }
    cli::tout << "Remote SDK successfully initialized.\n\n";

    cli::tout << "Enumerate connected camera devices...\n";
    SDK::ICrEnumCameraObjectInfo *camera_list = nullptr;
    // auto enum_status = cr_lib->EnumCameraObjects(&camera_list, 3);
    auto enum_status = SDK::EnumCameraObjects(&camera_list);
    if (CR_FAILED(enum_status) || camera_list == nullptr)
    {
        cli::tout << "No cameras detected. Connect a camera and retry.\n";
        // cr_lib->Release();
        SDK::Release();
        std::exit(EXIT_FAILURE);
    }
    auto ncams = camera_list->GetCount();
    cli::tout << "Camera enumeration successful. " << ncams << " detected.\n\n";

    for (CrInt32u i = 0; i < ncams; ++i)
    {
        auto camera_info = camera_list->GetCameraObjectInfo(i);
        cli::text conn_type(camera_info->GetConnectionTypeName());
        cli::text model(camera_info->GetModel());
        cli::text id = TEXT("");
        if (TEXT("IP") == conn_type)
        {
            cli::NetworkInfo ni = cli::parse_ip_info(camera_info->GetId(), camera_info->GetIdSize());
            id = ni.mac_address;
        }
        else
            id = ((TCHAR *)camera_info->GetId());
        cli::tout << '[' << i + 1 << "] " << model.data() << " (" << id.data() << ")\n";
    }

    cli::tout << std::endl
              << "Connect to camera with input number...\n";

    CrInt32u no = 1;

    typedef std::shared_ptr<cli::CameraDevice> CameraDevicePtr;
    typedef std::vector<CameraDevicePtr> CameraDeviceList;
    CameraDeviceList cameraList; // all
    std::int32_t cameraNumUniq = 1;
    std::int32_t selectCamera = 1;

    cli::tout << "Connect to selected camera...\n";
    auto *camera_info = camera_list->GetCameraObjectInfo(no - 1);

    cli::tout << "Create camera SDK camera callback object.\n";
    CameraDevicePtr camera = CameraDevicePtr(new cli::CameraDevice(cameraNumUniq, nullptr, camera_info));
    cameraList.push_back(camera); // add 1st

    cli::tout << "Release enumerated camera list.\n";
    camera_list->Release();

    auto connect_status = camera->connect();
    if (!connect_status)
    {
        cli::tout << "Camera connection failed to initiate. Abort.\n";
        // cr_lib->Release();
        SDK::Release();
        std::exit(EXIT_FAILURE);
    }
    cli::tout << "Camera connection successfully initiated!\n\n";

    camera_ready = true;
    return camera;
}

int main()
{
    signal(SIGINT,sigint_handler);
    camera = camera_init();
    printf("--------------------------------------camera init done\n");
    while(camera->is_connected()==false){
        // printf(".");
        usleep(100000);
    }
    // int c = 0;
    // while(camera->get_live_view_buffer_size()==0){
    //     camera->get_live_view(c++);
    //     usleep(1000000);
    // }

    printf("Get live view done\n");
    
    

    if(!all_threads_init()){
        perror("thread create");
        exit(1);
    }

    gstreamer_init((char*)stream_ip,stream_port);

    while(1){
        usleep(100000);
    }
    cli::tout << "Release SDK resources.\n";
    // cr_lib->Release();
    SDK::Release();
    cli::tout << "Exiting application.\n";
    std::exit(EXIT_SUCCESS);
}

bool all_threads_init(){
    int rc;
    rc = pthread_create(&thrd_get_buffer, NULL, &get_buffer_handle, (int*)1);
    if(rc){
        printf("Error: khong the tao thread get buffer!\n");
        return false;
    }
    printf("Get buffer thread created\n");
    rc = pthread_create(&thrd_push_buffer, NULL, &push_buffer_handle, (int*)3);
    if(rc){
        printf("Error: khong the tao thread push buffer!\n");
        return false;
    }
    printf("Push buffer thread created\n");

    // rc = pthread_create(&thrd_send_buffer, NULL, &send_buffer_handle, (int *)5);
    // if (rc){
    //     printf("Error: Khong the tao thread send buffer!\n");
    //     return false;
    // }
    // printf("Send Buffer thread created\n");
    return true;
}

void gstreamer_init(char* host_addr, int host_port){
    printf("gstreamer initial...\n");

    pipeline = nullptr;
    GError              *error = nullptr;
    gboolean             success = true;
    GstStateChangeReturn state_change_ret;

    char launch_str[700] = {0};

    if (1)
    {
        loop = g_main_loop_new(nullptr,false);
        #if 0
        sprintf(launch_str,"appsrc name=mysrc "
                               "! jpegdec "
                               "! video/x-raw,width=%d,height=%d "
                               "! videoconvert "
                               "! tee name=t "
                               "t. ! queue "
                               // "! x264enc speed-preset=1 byte-stream=true "
                               "! x264enc tune=zerolatency bitrate=4096 speed-preset=1 "
                               "! h264parse "
                               // "! video/x-h264,width=1024,height=680 ! rtph264pay ! udpsink host=192.168.140.3 port=5004 "
                               "! rtph264pay ! udpsink host=192.168.140.3 port=5004 "
                               // "t. ! queue "
                               // "! x264enc speed-preset=1 "
                               // // "! video/x-h264,width=1024,height=680 "//,framerate=30/1, bitrate=4000000 "
                               // "! appsink name=sink emit-signals=true sync=false max-buffers=1 drop=true"
                              ,1024,680);
        #else
        // sprintf(launch_str,"videotestsrc pattern=pinwheel "
        sprintf(launch_str,"appsrc name=mysrc is-live=true do-timestamp=true "
        // sprintf(launch_str,"filesrc location=./LiveView0.JPG "
                               "! jpegdec "
                               "! video/x-raw,width=%d,height=%d "
                               "! clockoverlay "
                               "! videoconvert "
                               "! tee name=t "
                               "t. ! queue "
                               "! x264enc speed-preset=1 byte-stream=true "
                               "! h264parse "
                               "! rtph264pay ! udpsink host=192.168.140.3 port=5004 "
                               // "! mp4mux "
                               // "! filesink location=./out.mp4"

                               // "! x264enc speed-preset=1 byte-stream=true "
                               // "! video/x-h264,width=1024,height=680 ! rtph264pay ! udpsink host=192.168.140.3 port=5004 "
                               // "t. ! queue "
                               // "! x264enc speed-preset=1 "
                               // // "! video/x-h264,width=1024,height=680 "//,framerate=30/1, bitrate=4000000 "
                               // // "! appsink name=sink emit-signals=true sync=false max-buffers=1 drop=true"
                               // "! fakesink"
                              ,1024,680);
        #endif
        gst_init(NULL, NULL);

        // Check pipeline
        pipeline = gst_parse_launch(launch_str, &error);

        if(error != nullptr) {
            g_print("could not construct pipeline: %s\n", error->message);
            //g_error_free(error);
            success = false;
        }
        //Get source
        appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "mysrc");

        // g_object_set (G_OBJECT (appsrc),
        //     "stream-type", 0,
        //     "is-live", true,            
        //     "format", GST_FORMAT_TIME, NULL);

        g_signal_connect (appsrc, "need-data", G_CALLBACK(cb_need_data), NULL);
        g_signal_connect (appsrc, "enough-data", G_CALLBACK(cb_enough_data), NULL);

#if 0
        // Get sink
        sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
        /**
        * @brief Get sink signals and check for a preroll
        *  If preroll exists, we do have a new frame
        */

        gst_app_sink_set_emit_signals((GstAppSink*)sink, true);
        gst_app_sink_set_drop((GstAppSink*)sink, true);
        gst_app_sink_set_max_buffers((GstAppSink*)sink, 1);
        GstAppSinkCallbacks callbacks = { nullptr, new_preroll, new_sample };
        gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, nullptr, nullptr);
#endif
        // Declare bus
        GstBus *bus;
        bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
        gst_bus_add_watch(bus, my_bus_callback, nullptr);
        gst_object_unref(bus);

        state_change_ret = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

        if (state_change_ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Failed to start pipeline\n");

            reboot_system();

            success = FALSE;
        }    
        if(success){
            // if ((s = socket(AF_INET,SOCK_DGRAM,0))<0)
            // {
            //     perror("socket");
            //     exit(1);
            // }
            // bzero(&addr,sizeof(addr));
            // addr.sin_family = AF_INET;
            // addr.sin_port = htons(host_port);
            // addr.sin_addr.s_addr = inet_addr(host_addr);

            m_pipeline_ready = true;

            printf("-----------------------------------------------main loop running\n");
            g_main_loop_run(loop);

        }
    }
}
bool white = false;
void feed_data_to_gstreamer(char* data, uint32_t size){
    GstFlowReturn ret;
    GstBuffer *buffer;
    static GstClockTime timestamp = 0;

    // int size = width*height;
    // printf("tp0 %d\n", size);
    buffer = gst_buffer_new_allocate(NULL, size, NULL);
    // printf("tp1\n");

    GstMapInfo info;
    if(!gst_buffer_map(buffer, &info, GST_MAP_WRITE)){
        printf("err0\n");
    }

    #if 1
    unsigned char* buf = info.data;
    memmove(buf, data, size);
    gst_buffer_unmap(buffer, &info);

    GST_BUFFER_PTS (buffer) = timestamp;
    // duration = gst_util_uint64_scale_int (1, GST_SECOND, fps);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 25);
    timestamp += GST_BUFFER_DURATION (buffer);

    #else
    /* this makes the image black/white */
    gst_buffer_memset (buffer, 0, white ? 0xff : 0x0, size);

    white = !white;

    // GST_BUFFER_PTS (buffer) = timestamp;
    // GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 2);

    // timestamp += GST_BUFFER_DURATION (buffer);
    #endif

    // ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
    g_signal_emit_by_name (GST_APP_SRC(appsrc), "push-buffer", buffer, &ret);
    
    if(ret == GST_FLOW_OK){
        // printf("feed data to gstreamer %d\n", size);
    }else{
        printf("feed_data failed\n");
    }
}

void *push_buffer_handle(void *threadid){
    char* buffer = new char[JPEG_SIZE];
    int32_t lengthOfBuffer = 0;
    int count = 0;
    while(!thrd_must_exit){
        if(m_pipeline_ready){
            // if(camera_ready){
            //     if(camera->is_connected()){

            //         camera->get_live_view(&buffer,lengthOfBuffer);
            //         if(lengthOfBuffer > 0 && is_need_data){
            //             feed_data_to_gstreamer(buffer,lengthOfBuffer);
            //             is_need_data = false;
            //                 printf("%s\n", __FUNCTION__);
            //         }
            //     }else{
            //         printf("Camera not connected\n");
            //     }
            // }else{
            //     printf("Camera is not ready\n");
            // }

            // printf("[%s]\n", __FUNCTION__);
            if (camera_ready)
            {
                if (camera->is_connected())
                {
                    // push buffer to gstreamer
                    int32_t _size = camera->get_live_view_buffer_size();
                    
                    if (_size > 0 )
                    {
                        if(is_need_data){
                            feed_data_to_gstreamer(camera->get_live_view_buffer(), _size);
                            _size = 0;
                            is_need_data = false;
                        }
                        else{
                            printf("is_need_data FALSE\n");
                        }
                    }else{
                        printf("_size < 0\n");
                    }
                }
                

            }
        }
        usleep(40000);
    }

    // if (pipeline != nullptr)
    // {
    //     gst_element_set_state(pipeline, GST_STATE_NULL);
    //     gst_object_unref(pipeline);
    //     gst_object_unref(appsrc);
    // }

    // if (loop != nullptr)
    // {
    //     g_main_loop_unref(loop);
    // }   

}

void *get_buffer_handle(void *threadid){
    int cnt = 0;

    while(!thrd_must_exit){
        if(m_pipeline_ready){
            
            try
            {
                if (camera->is_connected())
                {
                    // printf("%s\n",__FUNCTION__);
                    camera->get_live_view(cnt);

                    // if(camera->get_live_view_buffer_size() > 0){
                    //     char pathOfImg[100];
                    //     sprintf(pathOfImg,"./LiveView0.JPG",cnt);
                    //     cli::tout << pathOfImg << '\n';
                    //     std::ofstream __file(pathOfImg, std::ios::out | std::ios::binary);
                    //     if (!__file.bad())
                    //     {
                    //         __file.write((char*)camera->get_live_view_buffer(), camera->get_live_view_buffer_size());
                    //         __file.close();
                    //     }
                    //     else {
                    //         cli::tout << "can not write image\n";
                    //     }
                    //     cnt++;
                    // }


                }
                usleep(20000);
                // usleep(1000000);
            }
            catch (...)
            {
            };
        }
    }

    if (camera->is_connected())
        {
            cli::tout << "Initiate disconnect sequence.\n";
            auto disconnect_status = camera->disconnect();
            if (!disconnect_status)
            {
                // try again
                disconnect_status = camera->disconnect();
            }
            if (!disconnect_status)
                cli::tout << "Disconnect failed to initiate.\n";
            else
                cli::tout << "Disconnect successfully initiated!\n\n";
        }
        camera->release();

        SDK::Release();
}

void *send_buffer_handle(void *threadid){
    static GstMapInfo _map;

    while(!thrd_must_exit && m_pipeline_ready){

        if(!get_nxt_sample(_map)){
            usleep(1000);
            continue;
        }
        else{
            check_frame((uint8_t* )_map.data, _map.size);
        }
    
    }

    pthread_exit(NULL);
}

uint8_t get_nxt_sample(GstMapInfo& _sample){
    if(m_stream_sample.empty()){
        return 0;
    }
    _sample = m_stream_sample.front();
    m_stream_sample.pop();
    return 1;
}

void check_frame(uint8_t* _data, int _len){

    // auto start = high_resolution_clock::now(); 

    int i, j;
    i = 1;
    j = 0;

    // copy to local buffer
    uint8_t _local_buf[_len];
    // for(int i=0; i < _len; i++){
    //     _local_buf[i] = _data[i];
    // }

    int _byte_64_num = _len/8; // bytes to add
    int _write_idx = 0; // start pos to write
    int k;

    for(k = 0; k < _byte_64_num; k+=8){
        uint64_t* pbuff = (uint64_t*)&_local_buf[_write_idx];
        
        *pbuff = *(uint64_t*)&_data[k];

        _write_idx += 8;
    }

    for(k=k; k<=_len; k++)
    {        
        _local_buf[_write_idx] = _data[k];
        _write_idx++;
    }

    while(true){
        // while (memcmp(_local_buf + i, SPS_Head, FRAME_HEAD_LEN) != 0
        //    && memcmp(_local_buf + i, PPS_Head, FRAME_HEAD_LEN) != 0
        //    && memcmp(_local_buf + i, I_Head, FRAME_HEAD_LEN) != 0
        //    && memcmp(_local_buf + i, P_Head, FRAME_HEAD_LEN) != 0
        //    && memcmp(_local_buf + i, P_Head2, FRAME_HEAD_LEN) != 0) {

        //     i++;
        //     if (i > _len) {
        //         printf("i: no header found\n");
        //         return;
        //     }
        // }

        // j = i + FRAME_HEAD_LEN;
        // while (memcmp(_local_buf + j, SPS_Head, FRAME_HEAD_LEN) != 0
        //        && memcmp(_local_buf + j, PPS_Head, FRAME_HEAD_LEN) != 0
        //        && memcmp(_local_buf + j, I_Head, FRAME_HEAD_LEN) != 0
        //        && memcmp(_local_buf + j, P_Head, FRAME_HEAD_LEN) != 0
        //        && memcmp(_local_buf + j, P_Head2, FRAME_HEAD_LEN) != 0) {

        //     j++;
        //     if (j > _len) {
        //         // last frame in buffer
        //         j = _len;
        //         break;
        //     }
        // }
    #if 1
        int k;
        uint32_t sps_head   = 0x67010000;
        uint32_t pps_head   = 0x68010000;
        uint32_t i_head     = 0x65010000;
        uint32_t p_head     = 0x61010000;
        uint32_t p_head2    = 0x41010000;

        // for(k=i; k < _len; k++){
        //     uint32_t* pbuff = (uint32_t*)&_local_buf[k];
        //     if(*pbuff == sps_head 
        //         || *pbuff == pps_head
        //         || *pbuff == i_head
        //         || *pbuff == p_head
        //         || *pbuff == p_head2){

        //         i= k;
        //         j= 0;
        //         k= _len;
        //         break;
        //     }
        // }

        // for(k=i+FRAME_HEAD_LEN; k < _len; k++){
        //     uint32_t* pbuff = (uint32_t*)&_local_buf[k];
        //     if(*pbuff == sps_head 
        //         || *pbuff == pps_head
        //         || *pbuff == i_head
        //         || *pbuff == p_head
        //         || *pbuff == p_head2){

        //         j= k;
        //         k=_len;
        //         break;
        //     }
        // }
        // if(j ==0) j= _len;

        
        uint32_t* pbuff = (uint32_t*)&_local_buf[i];
        if(*pbuff == sps_head){
            j = i + 51;
        }else if(*pbuff == pps_head){
            j = i + 8;
        }else j = _len;

    #else
        i=0; j=_len;
    #endif

    #if 0
        printf("frame detected: buffer len: %d, i: %d, j: %d\n", _len, i , j);
        if (memcmp(_local_buf + i, SPS_Head, FRAME_HEAD_LEN) == 0) {
            printf("   send SPS frame, len = %d\n", j - i);
        
            while (j - i > MAX_SEND_PACK) {
                sendto(s, _local_buf + i, MAX_SEND_PACK, 0, (struct sockaddr *) &addr, addr_len);
                i += MAX_SEND_PACK;
            }

            sendto(s, _local_buf + i, j - i, 0, (struct sockaddr *) &addr, addr_len);
        
            // usleep(FRAME_GAP);
        } else if (memcmp(_local_buf + i, PPS_Head, FRAME_HEAD_LEN) == 0) {
            printf("   send PPS frame, len = %d\n", j - i);
        
            while (j - i > MAX_SEND_PACK) {
                sendto(s, _local_buf + i, MAX_SEND_PACK, 0, (struct sockaddr *) &addr, addr_len);
                i += MAX_SEND_PACK;
            }

            sendto(s, _local_buf + i, j - i, 0, (struct sockaddr *) &addr, addr_len);
        
            // usleep(FRAME_GAP);
        } else if (memcmp(_local_buf + i, I_Head, FRAME_HEAD_LEN) == 0) {
            printf("   send I frame, len = %d\n", j - i);
        
            while (j - i > MAX_SEND_PACK) {
                sendto(s, _local_buf + i, MAX_SEND_PACK, 0, (struct sockaddr *) &addr, addr_len);
                i += MAX_SEND_PACK;
            }

            sendto(s, _local_buf + i, j - i, 0, (struct sockaddr *) &addr, addr_len);
        
            // usleep(FRAME_GAP);
        } else if (memcmp(_local_buf + i, P_Head, FRAME_HEAD_LEN) == 0
                   || memcmp(_local_buf + i, P_Head2, FRAME_HEAD_LEN) == 0) {
            printf("   send P frame, len = %d\n", j - i);
        
            while (j - i > MAX_SEND_PACK) {
                sendto(s, _local_buf + i, MAX_SEND_PACK, 0, (struct sockaddr *) &addr, addr_len);
                i += MAX_SEND_PACK;
            }

            sendto(s, _local_buf + i, j - i, 0, (struct sockaddr *) &addr, addr_len);
        
            // usleep(FRAME_GAP);
        }
    #else
        // printf("frame detect, i: %d, j: %d, len: %d\n", i, j, (j - i));
        while (j - i > MAX_SEND_PACK) {
            sendto(s, _local_buf + i, MAX_SEND_PACK, 0, (struct sockaddr *) &addr, addr_len);
            i += MAX_SEND_PACK;
        }

        sendto(s, _local_buf + i, j - i, 0, (struct sockaddr *) &addr, addr_len);
        
    #endif

        i = j;
        if(j == _len) break;
        // usleep(FRAME_GAP);
    }

    auto stop = high_resolution_clock::now(); 
    auto duration = duration_cast<microseconds>(stop - start); 
    // cout << "frame size: " << _len << " , process time: " << duration.count() << endl;

}

void push_buffer_to_queue(char* data, uint32_t size){
    GstBuffer *buffer;
    static GstClockTime timestamp = 0;

    // int size = width*height;
    // printf("tp0 %d\n", size);
    buffer = gst_buffer_new_allocate(NULL, size, NULL);
    // printf("tp1\n");

    GstMapInfo info;
    if(!gst_buffer_map(buffer, &info, GST_MAP_WRITE)){
        printf("err0\n");
    }

    unsigned char* buf = info.data;
    memmove(buf, data, size);
    gst_buffer_unmap(buffer, &info);

    GST_BUFFER_PTS (buffer) = timestamp;
    // duration = gst_util_uint64_scale_int (1, GST_SECOND, fps);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 25);
    timestamp += GST_BUFFER_DURATION (buffer);

}