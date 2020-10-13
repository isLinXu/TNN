#include <fstream>
#include <string>
#include <cuda_runtime.h>
#include <sys/time.h>
#include "cuda_profiler_api.h"

#include "tnn/core/tnn.h"
#include "tnn/core/macro.h"
#include "tnn/utils/blob_converter.h"

// Helper functions
std::string fdLoadFile(std::string path) {
    std::ifstream file(path, std::ios::in);
    if (file.is_open()) {
        file.seekg(0, file.end);
        int size      = file.tellg();
        char* content = new char[size];

        file.seekg(0, file.beg);
        file.read(content, size);
        std::string fileContent;
        fileContent.assign(content, size);
        delete[] content;
        file.close();
        return fileContent;
    } else {
        return "";
    }
}

int main(int argc, char** argv) {
    if (argc < 10) {
        printf("how to run: %s proto model batch channel height width input_name output_name times\n", argv[0]);
        exit(-1);
    }

    int n = 1, c = 3, h = 224, w = 224;
    int times = 1;
    std::string output_name;
    std::string input_name;
    n = std::atoi(argv[3]);
    c = std::atoi(argv[4]);
    h = std::atoi(argv[5]);
    w = std::atoi(argv[6]);
    input_name = std::string(argv[7]);
    output_name = std::string(argv[8]);

    std::shared_ptr<TNN_NS::TNN> net_ = nullptr;
    std::shared_ptr<TNN_NS::Instance> instance_ = nullptr;
    TNN_NS::DeviceType device_type_ = TNN_NS::DEVICE_CUDA;

    auto proto = fdLoadFile(argv[1]);
    auto model = fdLoadFile(argv[2]);

    TNN_NS::Status status;
    TNN_NS::ModelConfig model_config;
    model_config.model_type = TNN_NS::MODEL_TYPE_TNN;
    model_config.params = {proto, model};
    auto net = std::make_shared<TNN_NS::TNN>();
    status = net->Init(model_config);
    if (status != TNN_NS::TNN_OK) {
        printf("instance.net init failed %d", (int)status);
        exit(-1);
    }
    net_ = net;

    std::vector<int> nchw = {n, c, h*2, w*2};
    TNN_NS::InputShapesMap shapeMap;
    shapeMap.insert(std::pair<std::string, TNN_NS::DimsVector>(input_name, nchw));
    TNN_NS::NetworkConfig network_config;
    network_config.device_type = device_type_;
    network_config.network_type = TNN_NS::NETWORK_TYPE_TENSORRT;
    auto instance = net_->CreateInst(network_config, status, shapeMap);
    instance_ = instance;

    std::vector<int> nchw2 = {n, c, h, w};
    TNN_NS::InputShapesMap newShapeMap;
    newShapeMap.insert(std::pair<std::string, TNN_NS::DimsVector>(input_name, nchw2));
    status = instance_->Reshape(newShapeMap);

    auto input_mat = std::make_shared<TNN_NS::Mat>(TNN_NS::DEVICE_NAIVE, TNN_NS::NCHW_FLOAT, nchw2);
    TNN_NS::MatConvertParam input_cvt_param;
    input_cvt_param.scale = {1.0 / (255 * 0.229), 1.0 / (255 * 0.224), 1.0 / (255 * 0.225), 0.0};
    input_cvt_param.bias = {-0.485 / 0.229, -0.456 / 0.224, -0.406 / 0.225, 0.0};
    auto input_data = input_mat->GetData();
    for (int i = 0; i < input_mat->GetBatch() * input_mat->GetChannel() * input_mat->GetHeight() * input_mat->GetWidth(); i++) {
        ((float*)input_data)[i] = 0.5;
    }
    status = instance_->SetInputMat(input_mat, input_cvt_param);
    if (status != TNN_NS::TNN_OK) {
        printf("set input failed %d", (int)status);
        exit(-1);
    }

    struct timezone zone;
    struct timeval time1;
    struct timeval time2;

    cudaDeviceSynchronize();
    gettimeofday(&time1, &zone);
    for (int i = 0; i < times; i++) {
        status = instance_->ForwardAsync(nullptr);
        if (status != TNN_NS::TNN_OK) {
            printf("forward failed %d\n", (int)status);
            exit(-1);
        }
    }
    cudaDeviceSynchronize();
    gettimeofday(&time2, &zone);
    printf("forward is done.\n");
    float delta = (time2.tv_sec - time1.tv_sec) * 1000.0 + (time2.tv_usec - time1.tv_usec) / 1000.0;
    printf("time cost: %g ms\n", delta/(float)times);

    TNN_NS::MatConvertParam output_cvt_param;
    std::shared_ptr<TNN_NS::Mat> output_mat = nullptr;
    status = instance_->GetOutputMat(output_mat, output_cvt_param, output_name, TNN_NS::DEVICE_NAIVE);
    if (status != TNN_NS::TNN_OK) {
        printf("get output failed %d\n", (int)status);
        exit(-1);
    }

    float* data = (float*)(output_mat->GetData());
    int output_size = output_mat->GetBatch() * output_mat->GetChannel() * output_mat->GetHeight() * output_mat->GetWidth();
    for (int i = 0; i < output_size; i++) {
        printf("%f\n", data[i]);
    }
    return 0;
}
