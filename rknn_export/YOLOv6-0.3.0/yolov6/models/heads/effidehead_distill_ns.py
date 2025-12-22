import torch
import torch.nn as nn
import torch.nn.functional as F
import math
from yolov6.layers.common import *
from yolov6.assigners.anchor_generator import generate_anchors
from yolov6.utils.general import dist2bbox


class Detect(nn.Module):
    export_rknn = True #导出rknn友好模型
    '''Efficient Decoupled Head for Cost-free Distillation.(FOR NANO/SMALL MODEL)
    '''
    def __init__(self, num_classes=80, num_layers=3, inplace=True, head_layers=None, use_dfl=True, reg_max=16):  # detection layer
        super().__init__()
        assert head_layers is not None
        self.nc = num_classes  # number of classes
        self.no = num_classes + 5  # number of outputs per anchor
        self.nl = num_layers  # number of detection layers
        self.grid = [torch.zeros(1)] * num_layers
        self.prior_prob = 1e-2
        self.inplace = inplace
        stride = [8, 16, 32]  # strides computed during build
        self.stride = torch.tensor(stride)
        self.use_dfl = use_dfl
        self.reg_max = reg_max
        self.proj_conv = nn.Conv2d(self.reg_max + 1, 1, 1, bias=False)
        self.grid_cell_offset = 0.5
        self.grid_cell_size = 5.0

        # Init decouple head
        self.stems = nn.ModuleList()
        self.cls_convs = nn.ModuleList()
        self.reg_convs = nn.ModuleList()
        self.cls_preds = nn.ModuleList()
        self.reg_preds = nn.ModuleList()
        self.reg_preds_lrtb = nn.ModuleList()

        # Efficient decoupled head layers
        for i in range(num_layers):
            idx = i*6
            self.stems.append(head_layers[idx])
            self.cls_convs.append(head_layers[idx+1])
            self.reg_convs.append(head_layers[idx+2])
            self.cls_preds.append(head_layers[idx+3])
            self.reg_preds.append(head_layers[idx+4])
            self.reg_preds_lrtb.append(head_layers[idx+5])

    def initialize_biases(self):

        for conv in self.cls_preds:
            b = conv.bias.view(-1, )
            b.data.fill_(-math.log((1 - self.prior_prob) / self.prior_prob))
            conv.bias = torch.nn.Parameter(b.view(-1), requires_grad=True)
            w = conv.weight
            w.data.fill_(0.)
            conv.weight = torch.nn.Parameter(w, requires_grad=True)

        for conv in self.reg_preds:
            b = conv.bias.view(-1, )
            b.data.fill_(1.0)
            conv.bias = torch.nn.Parameter(b.view(-1), requires_grad=True)
            w = conv.weight
            w.data.fill_(0.)
            conv.weight = torch.nn.Parameter(w, requires_grad=True)

        for conv in self.reg_preds_lrtb:
            b = conv.bias.view(-1, )
            b.data.fill_(1.0)
            conv.bias = torch.nn.Parameter(b.view(-1), requires_grad=True)
            w = conv.weight
            w.data.fill_(0.)
            conv.weight = torch.nn.Parameter(w, requires_grad=True)
        
        self.proj = nn.Parameter(torch.linspace(0, self.reg_max, self.reg_max + 1), requires_grad=False)
        self.proj_conv.weight = nn.Parameter(self.proj.view([1, self.reg_max + 1, 1, 1]).clone().detach(),
                                                   requires_grad=False)
    def _rknn_opt_head(self, x):
        # 存储3个尺度的最终输出张量（output1/output2/output3）
        output_for_rknn = []
         # self.nl=3（YOLOv6默认3个检测尺度：8/16/32倍下采样）
        for i in range(self.nl):
            # 步骤1：特征图浅层处理（Stem层，调整通道/维度，适配后续计算）
            # x[i] 张量形状：[1, 128, H, W]（H/W随尺度变化：44×80/22×40/11×20）
            x[i] = self.stems[i](x[i])
            
            # 步骤2：回归分支，预计目标框位置，上下左右，四个方向
            # reg_feat 张量形状：[1, 128, H, W]
            reg_feat = self.reg_convs[i](x[i])
            
            # reg_output 张量形状：[1, 4, H, W]（4通道对应：左/右/上/下偏移，定位目标框）
            reg_output = self.reg_preds_lrtb[i](reg_feat)

            # 步骤3：分类分支（预测目标类别）
            # cls_feat 张量形状：[1, 128, H, W]
            cls_feat = self.cls_convs[i](x[i])
            
            # cls_output 张量形状：[1, 80, H, W]（80通道对应COCO 80类原始得分）
            cls_output = self.cls_preds[i](cls_feat)
            
            # # 步骤4：Sigmoid激活 [0,1]范围内，激活函数，置信度
            cls_output = torch.sigmoid(cls_output) #npu支持sigmoid 消耗1ms以下
            
             # 步骤5：提取最高置信度（减少后处理计算量）
             # 操作：对80类置信度（dim=1通道维度）取最大值，keepdim=True保证张量维度不变
             # 提前在模型内计算，避免开发板C++后处理再遍历80类，提升速度
            conf_max,_ = cls_output.max(1, keepdim=True) 
            
            # 拼接张量数组
            # 操作：在通道维度（dim=1）拼接3个张量
            # out 张量形状：[1, 85, H, W]（4+1+80=85通道，包含所有检测信息）
            # 把分散的回归/分类信息打包成一个张量，减少NPU→CPU的数据传输次数（传输1个张量比3个快）
            out = torch.cat((reg_output,conf_max,cls_output),dim=1) #4(reg)+1(max)+80(cls)
            
            # 步骤7：收集多尺度输出
            # 把3个尺度（8/16/32倍下采样）的85通道张量存入列表
            # output_for_rknn 最终是3个张量：
            # - output1：[1,85,44,80]（8倍下采样，小目标）
            # - output2：[1,85,22,40]（16倍下采样，中目标）
            # - output3：[1,85,11,20]（32倍下采样，大目标）
            output_for_rknn.append( out )
            
        return output_for_rknn

    def forward(self, x):
        if self.export_rknn:
            return self._rknn_opt_head(x)
        if self.training:
            cls_score_list = []
            reg_distri_list = []
            reg_lrtb_list = []

            for i in range(self.nl):
                x[i] = self.stems[i](x[i])
                cls_x = x[i]
                reg_x = x[i]
                cls_feat = self.cls_convs[i](cls_x)
                cls_output = self.cls_preds[i](cls_feat)
                reg_feat = self.reg_convs[i](reg_x)
                reg_output = self.reg_preds[i](reg_feat)
                reg_output_lrtb = self.reg_preds_lrtb[i](reg_feat)

                cls_output = torch.sigmoid(cls_output)
                cls_score_list.append(cls_output.flatten(2).permute((0, 2, 1)))
                reg_distri_list.append(reg_output.flatten(2).permute((0, 2, 1)))
                reg_lrtb_list.append(reg_output_lrtb.flatten(2).permute((0, 2, 1)))
            
            cls_score_list = torch.cat(cls_score_list, axis=1)
            reg_distri_list = torch.cat(reg_distri_list, axis=1)
            reg_lrtb_list = torch.cat(reg_lrtb_list, axis=1)

            return x, cls_score_list, reg_distri_list, reg_lrtb_list
        else:
            cls_score_list = []
            reg_lrtb_list = []
            anchor_points, stride_tensor = generate_anchors(
                x, self.stride, self.grid_cell_size, self.grid_cell_offset, device=x[0].device, is_eval=True, mode='af')

            for i in range(self.nl):
                b, _, h, w = x[i].shape
                l = h * w
                x[i] = self.stems[i](x[i])
                cls_x = x[i]
                reg_x = x[i]
                cls_feat = self.cls_convs[i](cls_x)
                cls_output = self.cls_preds[i](cls_feat)
                reg_feat = self.reg_convs[i](reg_x)
                reg_output_lrtb = self.reg_preds_lrtb[i](reg_feat)
                              
                cls_output = torch.sigmoid(cls_output)
                cls_score_list.append(cls_output.reshape([b, self.nc, l]))
                reg_lrtb_list.append(reg_output_lrtb.reshape([b, 4, l]))
            
            cls_score_list = torch.cat(cls_score_list, axis=-1).permute(0, 2, 1)
            reg_lrtb_list = torch.cat(reg_lrtb_list, axis=-1).permute(0, 2, 1)

            pred_bboxes = dist2bbox(reg_lrtb_list, anchor_points, box_format='xywh')
            pred_bboxes *= stride_tensor
            return torch.cat(
                [
                    pred_bboxes,
                    torch.ones((b, pred_bboxes.shape[1], 1), device=pred_bboxes.device, dtype=pred_bboxes.dtype),
                    cls_score_list
                ],
                axis=-1)


def build_effidehead_layer(channels_list, num_anchors, num_classes, reg_max=16):
    head_layers = nn.Sequential(
        # stem0
        Conv(
            in_channels=channels_list[6],
            out_channels=channels_list[6],
            kernel_size=1,
            stride=1
        ),
        # cls_conv0
        Conv(
            in_channels=channels_list[6],
            out_channels=channels_list[6],
            kernel_size=3,
            stride=1
        ),
        # reg_conv0
        Conv(
            in_channels=channels_list[6],
            out_channels=channels_list[6],
            kernel_size=3,
            stride=1
        ),
        # cls_pred0
        nn.Conv2d(
            in_channels=channels_list[6],
            out_channels=num_classes * num_anchors,
            kernel_size=1
        ),
        # reg_pred0
        nn.Conv2d(
            in_channels=channels_list[6],
            out_channels=4 * (reg_max + num_anchors),
            kernel_size=1
        ),
        # reg_pred0_1
        nn.Conv2d(
            in_channels=channels_list[6],
            out_channels=4 * (num_anchors),
            kernel_size=1
        ),
        # stem1
        Conv(
            in_channels=channels_list[8],
            out_channels=channels_list[8],
            kernel_size=1,
            stride=1
        ),
        # cls_conv1
        Conv(
            in_channels=channels_list[8],
            out_channels=channels_list[8],
            kernel_size=3,
            stride=1
        ),
        # reg_conv1
        Conv(
            in_channels=channels_list[8],
            out_channels=channels_list[8],
            kernel_size=3,
            stride=1
        ),
        # cls_pred1
        nn.Conv2d(
            in_channels=channels_list[8],
            out_channels=num_classes * num_anchors,
            kernel_size=1
        ),
        # reg_pred1
        nn.Conv2d(
            in_channels=channels_list[8],
            out_channels=4 * (reg_max + num_anchors),
            kernel_size=1
        ),
        # reg_pred1_1
        nn.Conv2d(
            in_channels=channels_list[8],
            out_channels=4 * (num_anchors),
            kernel_size=1
        ),
        # stem2
        Conv(
            in_channels=channels_list[10],
            out_channels=channels_list[10],
            kernel_size=1,
            stride=1
        ),
        # cls_conv2
        Conv(
            in_channels=channels_list[10],
            out_channels=channels_list[10],
            kernel_size=3,
            stride=1
        ),
        # reg_conv2
        Conv(
            in_channels=channels_list[10],
            out_channels=channels_list[10],
            kernel_size=3,
            stride=1
        ),
        # cls_pred2
        nn.Conv2d(
            in_channels=channels_list[10],
            out_channels=num_classes * num_anchors,
            kernel_size=1
        ),
        # reg_pred2
        nn.Conv2d(
            in_channels=channels_list[10],
            out_channels=4 * (reg_max + num_anchors),
            kernel_size=1
        ),
        # reg_pred2_1
        nn.Conv2d(
            in_channels=channels_list[10],
            out_channels=4 * (num_anchors),
            kernel_size=1
        )
    )
    return head_layers
