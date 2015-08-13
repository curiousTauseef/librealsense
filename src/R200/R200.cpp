#include "R200.h"

#ifdef USE_UVC_DEVICES
#include "CameraHeader.h"
#include "XU.h"
#include "SPI.h"

using namespace rs;

namespace r200
{
    R200Camera::R200Camera(uvc_device_t * device, int idx) : UVCCamera(device, idx)
    {

    }

    R200Camera::~R200Camera()
    {

    }

    //@tofix protect against calling this multiple times
    bool R200Camera::ConfigureStreams()
    {
        if (streamingModeBitfield == 0)
            throw std::invalid_argument("No streams have been configured...");

        //@tofix: Test for successful open
        if (streamingModeBitfield & RS_STREAM_DEPTH)
        {
            StreamInterface * stream = new StreamInterface();
            stream->camera = this;

            bool status = OpenStreamOnSubdevice(hardware, stream->uvcHandle, 1);
            streamInterfaces.insert(std::pair<int, StreamInterface *>(RS_STREAM_DEPTH, stream));
        }

        if (streamingModeBitfield & RS_STREAM_RGB)
        {
            StreamInterface * stream = new StreamInterface();
            stream->camera = this;

            bool status = OpenStreamOnSubdevice(hardware, stream->uvcHandle, 2);
            streamInterfaces.insert(std::pair<int, StreamInterface *>(RS_STREAM_RGB, stream));
        }

        GetUSBInfo(hardware, usbInfo);
        std::cout << "Serial Number: " << usbInfo.serial << std::endl;
        std::cout << "USB VID: " << usbInfo.vid << std::endl;
        std::cout << "USB PID: " << usbInfo.pid << std::endl;

        auto oneTimeInitialize = [&](uvc_device_handle_t * uvc_handle)
        {
            spiInterface.reset(new SPI_Interface(uvc_handle));

            spiInterface->Initialize();

            //uvc_print_diag(uvc_handle, stderr);

            std::cout << "Firmware Revision: " << GetFirmwareVersion(uvc_handle) << std::endl;

            spiInterface->LogDebugInfo();

            if (!SetStreamIntent(uvc_handle, streamingModeBitfield))
            {
                throw std::runtime_error("Could not set stream intent. Replug camera?");
            }
        };

        // We only need to do this once, so check if any stream has been configured
        if (streamInterfaces[RS_STREAM_DEPTH]->uvcHandle)
        {
            //uvc_print_stream_ctrl(&streamInterfaces[STREAM_DEPTH]->ctrl, stderr);
            oneTimeInitialize(streamInterfaces[RS_STREAM_DEPTH]->uvcHandle);
        }

        else if (streamInterfaces[RS_STREAM_RGB]->uvcHandle)
        {
           //uvc_print_stream_ctrl(&streamInterfaces[STREAM_RGB]->ctrl, stderr);
           oneTimeInitialize(streamInterfaces[RS_STREAM_RGB]->uvcHandle);
        }

        return true;
    }

    void R200Camera::StartStream(int streamIdentifier, const StreamConfiguration & c)
    {
        //  if (isStreaming) throw std::runtime_error("Camera is already streaming");

        auto stream = streamInterfaces[streamIdentifier];

        if (stream->uvcHandle)
        {
            stream->fmt = static_cast<uvc_frame_format>(c.format);

            uvc_error_t status = uvc_get_stream_ctrl_format_size(stream->uvcHandle, &stream->ctrl, stream->fmt, c.width, c.height, c.fps);

            if (status < 0)
            {
                uvc_perror(status, "uvc_get_stream_ctrl_format_size");
                throw std::runtime_error("Open camera_handle Failed");
            }

            //@tofix - check streaming mode as well
            if (c.format == FrameFormat::Z16)
            {
                depthFrame.reset(new TripleBufferedFrame(c.width, c.height, 2));
                zConfig = c;
            }

            else if (c.format == FrameFormat::YUYV)
            {
                colorFrame.reset(new TripleBufferedFrame(c.width, c.height, 3));
                rgbConfig = c;
            }

            uvc_error_t startStreamResult = uvc_start_streaming(stream->uvcHandle, &stream->ctrl, &UVCCamera::cb, stream, 0);

            if (startStreamResult < 0)
            {
                uvc_perror(startStreamResult, "start_stream");
                throw std::runtime_error("Could not start stream");
            }
        }

        //@tofix - else what?

    }

    void R200Camera::StopStream(int streamNum)
    {
        //@tofix - uvc_stream_stop with a real stream handle -> index with map that we have
        //uvc_stop_streaming(deviceHandle);
    }

    rs_intrinsics R200Camera::GetStreamIntrinsics(int stream)
    {
        auto calib = spiInterface->GetCalibration();
        auto lr = calib.modesLR[0]; // Assumes 628x468 for now
        auto t = calib.intrinsicsThird[1]; // Assumes 640x480 for now
        switch(stream)
        {
        case RS_STREAM_DEPTH: return {{lr.rw-12,lr.rh-12},{lr.rfx,lr.rfy},{lr.rpx-6,lr.rpy-6},{1,0,0,0,0}};
        case RS_STREAM_RGB: return {{t.w,t.h},{t.fx,t.fy},{t.px,t.py},{t.k[0],t.k[1],t.k[2],t.k[3],t.k[4]}};
        default: throw std::runtime_error("unsupported stream");
        }
    }

    rs_extrinsics R200Camera::GetStreamExtrinsics(int from, int to)
    {
        auto calib = spiInterface->GetCalibration();
        if(from == RS_STREAM_DEPTH && to == RS_STREAM_RGB)
        {
            rs_extrinsics extrin;
            for(int i=0; i<9; ++i) extrin.rotation[i] = (float)calib.Rthird[0][i];
            extrin.translation[0] = extrin.rotation[0]*calib.T[0][0] + extrin.rotation[1]*calib.T[0][1] + extrin.rotation[2]*calib.T[0][2];
            extrin.translation[1] = extrin.rotation[3]*calib.T[0][0] + extrin.rotation[4]*calib.T[0][1] + extrin.rotation[5]*calib.T[0][2];
            extrin.translation[2] = extrin.rotation[6]*calib.T[0][0] + extrin.rotation[7]*calib.T[0][1] + extrin.rotation[8]*calib.T[0][2];
            return extrin;
        }
        else throw std::runtime_error("unsupported streams");
    }
} // end namespace r200
#endif
