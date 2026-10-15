#include "h264parser.h"

#include <QtGlobal>

namespace H264Parser {

QString frameTypeName(FrameType type)
{
    switch (type) {
    case FrameType::I: return QStringLiteral("I");
    case FrameType::P: return QStringLiteral("P");
    case FrameType::B: return QStringLiteral("B");
    case FrameType::Idr: return QStringLiteral("IDR");
    default: return QStringLiteral("--");
    }
}

// ---------- Annex-B 切分 ----------

QList<Nalu> splitAnnexB(const QByteArray &data)
{
    QList<Nalu> result;
    const int size = data.size();
    int prevDataStart = -1;  // 上一个 NALU 数据起点（起始码之后）
    int prevCodeStart = 0;   // 上一个 NALU 起始码起点（供 offset 记录）
    int prevCodeLen = 0;
    int i = 0;
    while (i + 2 < size) {
        if (uchar(data[i]) == 0 && uchar(data[i + 1]) == 0 && uchar(data[i + 2]) == 1) {
            int codeStart = i;
            int codeLen = 3;
            if (i > 0 && uchar(data[i - 1]) == 0) {
                codeStart = i - 1;  // 00 00 00 01
                codeLen = 4;
            }
            if (prevDataStart >= 0) {
                // 上一 NALU 数据止于本起始码之前，剔除尾随零（trailing_zero_8bits）
                int dataEnd = codeStart;
                while (dataEnd > prevDataStart && uchar(data[dataEnd - 1]) == 0) {
                    --dataEnd;
                }
                Nalu nalu;
                nalu.startCodeLen = prevCodeLen;
                nalu.offset = prevCodeStart;
                nalu.data = data.mid(prevDataStart, dataEnd - prevDataStart);
                result.append(nalu);
            }
            prevDataStart = codeStart + codeLen;
            prevCodeStart = codeStart;
            prevCodeLen = codeLen;
            i = codeStart + codeLen;
        } else {
            ++i;
        }
    }
    if (prevDataStart >= 0) {
        int dataEnd = size;
        while (dataEnd > prevDataStart && uchar(data[dataEnd - 1]) == 0) {
            --dataEnd;
        }
        Nalu nalu;
        nalu.startCodeLen = prevCodeLen;
        nalu.offset = prevCodeStart;
        nalu.data = data.mid(prevDataStart, dataEnd - prevDataStart);
        result.append(nalu);
    }
    return result;
}

// ---------- NAL 头 ----------

NaluHeader parseNaluHeader(const QByteArray &nalu)
{
    NaluHeader h;
    if (nalu.isEmpty()) {
        return h;
    }
    const uchar b = uchar(nalu[0]);
    h.forbiddenZeroBit = b & 0x80;
    h.nalRefIdc = (b >> 5) & 0x03;
    h.type = b & 0x1F;
    h.valid = !h.forbiddenZeroBit;
    return h;
}

QString naluTypeName(int type)
{
    switch (type) {
    case 1: return QStringLiteral("非IDR切片");
    case 2: return QStringLiteral("数据分区A");
    case 3: return QStringLiteral("数据分区B");
    case 4: return QStringLiteral("数据分区C");
    case 5: return QStringLiteral("IDR切片");
    case 6: return QStringLiteral("SEI(附加增强信息)");
    case 7: return QStringLiteral("SPS(序列参数集)");
    case 8: return QStringLiteral("PPS(图像参数集)");
    case 9: return QStringLiteral("AUD(访问单元分隔)");
    case 10: return QStringLiteral("序列结束");
    case 11: return QStringLiteral("码流结束");
    case 12: return QStringLiteral("填充");
    case 13: return QStringLiteral("SPS扩展");
    case 14: return QStringLiteral("前缀单元");
    case 15: return QStringLiteral("子集SPS");
    case 19: return QStringLiteral("辅助编码切片");
    case 20: return QStringLiteral("扩展切片");
    default: return QStringLiteral("未知(%1)").arg(type);
    }
}

FrameType classifyFrame(const QByteArray &nalu)
{
    if (nalu.isEmpty()) {
        return FrameType::Unknown;
    }
    const int type = uchar(nalu[0]) & 0x1F;
    if (type == 5) {
        return FrameType::Idr;
    }
    if (type != 1) {
        return FrameType::Unknown;  // 参数集/SEI/分区等不参与帧统计
    }
    BitReader br(nalu, 8);
    br.readUe();  // first_mb_in_slice
    const quint32 sliceType = br.readUe();
    if (br.overrun() || sliceType > 9) {
        return FrameType::Unknown;
    }
    switch (sliceType % 5) {
    case 0: return FrameType::P;
    case 1: return FrameType::B;
    case 2: return FrameType::I;
    case 3: return FrameType::I;  // SP 切换片
    case 4: return FrameType::I;  // SI 切换片
    default: return FrameType::Unknown;
    }
}

// ---------- BitReader ----------

BitReader::BitReader(const QByteArray &data, int bitOffset)
    : m_data(data), m_bitPos(bitOffset)
{
    // 防竞争字节（emulation prevention）：字节流中 00 00 03 的 03 不属于 RBSP，
    // 建立映射表，读位时自动跳过（H.264 7.4.1.1）
    for (int i = 0; i < data.size(); ++i) {
        if (i >= 2 && uchar(data[i - 2]) == 0 && uchar(data[i - 1]) == 0
            && uchar(data[i]) == 3) {
            continue;  // 03 防竞争字节，不计入 RBSP
        }
        m_rbspToNal.append(i);
    }
}

quint32 BitReader::readBits(int n)
{
    if (n == 0) {
        return 0;  // ue(0) 的 0 位后缀，不视为越界
    }
    if (n > 32 || m_bitPos + n > m_rbspToNal.size() * 8) {
        m_overrun = true;
        return 0;
    }
    quint32 v = 0;
    for (int i = 0; i < n; ++i) {
        const int rbspByte = m_bitPos >> 3;
        const int nalIdx = m_rbspToNal.at(rbspByte);
        const int bitIdx = 7 - (m_bitPos & 7);
        v = (v << 1) | ((uchar(m_data.at(nalIdx)) >> bitIdx) & 1);
        ++m_bitPos;
    }
    return v;
}

quint32 BitReader::readUe()
{
    int leadingZeros = 0;
    while (leadingZeros < 32 && readBits(1) == 0 && !m_overrun) {
        ++leadingZeros;
    }
    if (m_overrun || leadingZeros >= 32) {
        m_overrun = true;
        return 0;
    }
    const quint32 suffix = readBits(leadingZeros);
    return (quint32(1) << leadingZeros) - 1 + suffix;
}

qint32 BitReader::readSe()
{
    const quint32 ue = readUe();
    if (m_overrun) {
        return 0;
    }
    const qint32 k = qint32((ue + 1) / 2);
    return (ue % 2) ? k : -k;
}

// ---------- SPS ----------

QString profileName(int profileIdc)
{
    switch (profileIdc) {
    case 44: return QStringLiteral("CAVLC 4:4:4");
    case 66: return QStringLiteral("Baseline");
    case 77: return QStringLiteral("Main");
    case 83: return QStringLiteral("Scalable Baseline");
    case 86: return QStringLiteral("Scalable High");
    case 88: return QStringLiteral("Extended");
    case 100: return QStringLiteral("High");
    case 110: return QStringLiteral("High 10");
    case 118: return QStringLiteral("Multiview High");
    case 122: return QStringLiteral("High 4:2:2");
    case 128: return QStringLiteral("Stereo High");
    case 134: return QStringLiteral("MFC High");
    case 135: return QStringLiteral("MFC Depth High");
    case 138: return QStringLiteral("Multiview Depth High");
    case 139: return QStringLiteral("Enhanced Multiview Depth High");
    case 244: return QStringLiteral("High 4:4:4");
    default: return QStringLiteral("未知(%1)").arg(profileIdc);
    }
}

QString levelName(int levelIdc)
{
    if (levelIdc == 9) {
        return QStringLiteral("1b");
    }
    if (levelIdc >= 10 && levelIdc <= 62) {
        return QStringLiteral("%1.%2").arg(levelIdc / 10).arg(levelIdc % 10);
    }
    return QStringLiteral("未知(%1)").arg(levelIdc);
}

static bool isHighProfile(int profileIdc)
{
    switch (profileIdc) {
    case 100: case 110: case 122: case 244:
    case 44: case 83: case 86: case 118:
    case 128: case 138: case 139: case 134: case 135:
        return true;
    default:
        return false;
    }
}

SpsInfo parseSps(const QByteArray &nalu)
{
    SpsInfo info;
    if (nalu.size() < 4 || (uchar(nalu[0]) & 0x1F) != 7) {
        return info;
    }
    BitReader br(nalu, 8);
    info.profileIdc = int(br.readBits(8));
    br.readBits(8);  // constraint flags + reserved
    info.levelIdc = int(br.readBits(8));
    br.readUe();  // seq_parameter_set_id
    if (isHighProfile(info.profileIdc)) {
        info.chromaFormatIdc = int(br.readUe());
        if (info.chromaFormatIdc == 3) {
            br.readBits(1);  // separate_colour_plane_flag
        }
        br.readUe();  // bit_depth_luma_minus8
        br.readUe();  // bit_depth_chroma_minus8
        br.readBits(1);  // qpprime_y_zero_transform_bypass_flag
        if (br.readBits(1)) {  // seq_scaling_matrix_present_flag
            br.readBits(1);  // 缩放矩阵语法复杂，调试工具放弃后续解析（触发 overrun）
            return info;    // valid 保持 false
        }
    }
    br.readUe();  // log2_max_frame_num_minus4
    const int picOrderCntType = int(br.readUe());
    if (picOrderCntType == 0) {
        br.readUe();  // log2_max_pic_order_cnt_lsb_minus4
    } else if (picOrderCntType == 1) {
        br.readBits(1);  // delta_pic_order_always_zero_flag
        br.readSe();     // offset_for_non_ref_pic
        br.readSe();     // offset_for_top_to_bottom_field
        const int n = int(br.readUe());
        for (int i = 0; i < n; ++i) {
            br.readSe();
        }
    }
    info.refFrames = int(br.readUe());  // max_num_ref_frames
    br.readBits(1);  // gaps_in_frame_num_value_allowed_flag
    const int widthInMbs = int(br.readUe()) + 1;
    const int heightInMapUnits = int(br.readUe()) + 1;
    info.frameMbsOnly = br.readBits(1);
    if (!info.frameMbsOnly) {
        br.readBits(1);  // mb_adaptive_frame_field_flag
    }
    br.readBits(1);  // direct_8x8_inference_flag
    int cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
    if (br.readBits(1)) {  // frame_cropping_flag
        cropLeft = int(br.readUe());
        cropRight = int(br.readUe());
        cropTop = int(br.readUe());
        cropBottom = int(br.readUe());
    }
    // 裁剪单位：4:2:0/4:2:2 为 2，4:4:4 与单色为 1（见 7.4.2.1.1）
    const int cropUnitX = (info.chromaFormatIdc == 0) ? 1 : 2;
    const int cropUnitY = (info.chromaFormatIdc == 0 || info.chromaFormatIdc == 3) ? 1 : 2;
    const int frameHeightInMbs = (info.frameMbsOnly ? 1 : 2) * heightInMapUnits;
    info.width = widthInMbs * 16 - (cropLeft + cropRight) * cropUnitX;
    info.height = frameHeightInMbs * 16 - (cropTop + cropBottom) * cropUnitY;

    const bool coreOk = !br.overrun() && info.width > 0 && info.height > 0
                        && info.width <= 16384 && info.height <= 16384;
    if (br.readBits(1)) {  // vui_parameters_present_flag
        // VUI 只需 timing_info（帧率），其余字段按语法跳过
        if (br.readBits(1)) {  // aspect_ratio_info_present_flag
            const int aspectRatioIdc = int(br.readBits(8));
            if (aspectRatioIdc == 255) {  // Extended_SAR
                br.readBits(16);
                br.readBits(16);
            }
        }
        if (br.readBits(1)) {  // overscan_info_present_flag
            br.readBits(1);
        }
        if (br.readBits(1)) {  // video_signal_type_present_flag
            br.readBits(3);
            br.readBits(1);
            if (br.readBits(1)) {  // colour_description_present_flag
                br.readBits(8);
                br.readBits(8);
                br.readBits(8);
            }
        }
        if (br.readBits(1)) {  // chroma_loc_info_present_flag
            br.readUe();
            br.readUe();
        }
        if (br.readBits(1)) {  // timing_info_present_flag
            const quint32 numUnitsInTick = br.readBits(32);
            const quint32 timeScale = br.readBits(32);
            br.readBits(1);  // fixed_frame_rate_flag
            if (!br.overrun() && numUnitsInTick > 0) {
                // 帧率 = time_scale / (2 * num_units_in_tick)
                info.frameRate = double(timeScale) / (2.0 * double(numUnitsInTick));
            }
        }
    }
    info.profileName = profileName(info.profileIdc);
    info.levelName = levelName(info.levelIdc);
    // VUI 越界不影响核心字段（宽高/profile/level），核心解析失败则整体无效
    info.valid = coreOk;
    return info;
}

// ---------- PPS ----------

PpsInfo parsePps(const QByteArray &nalu)
{
    PpsInfo info;
    if (nalu.size() < 2 || (uchar(nalu[0]) & 0x1F) != 8) {
        return info;
    }
    BitReader br(nalu, 8);
    info.ppsId = int(br.readUe());
    info.spsId = int(br.readUe());
    info.entropyCodingModeFlag = br.readBits(1);
    br.readBits(1);  // bottom_field_pic_order_in_frame_present_flag
    if (br.readUe() > 0) {  // num_slice_groups_minus1
        return info;  // FMO 语法复杂，不支持则视为无效
    }
    br.readUe();  // num_ref_idx_l0_default_active_minus1
    br.readUe();  // num_ref_idx_l1_default_active_minus1
    info.weightedPredFlag = br.readBits(1);
    br.readBits(2);  // weighted_bipred_idc
    info.picInitQpMinus26 = int(br.readSe());
    br.readSe();  // pic_init_qs_minus26
    br.readSe();  // chroma_qp_index_offset
    info.deblockingFilterControlPresentFlag = br.readBits(1);
    info.constrainedIntraPredFlag = br.readBits(1);
    info.redundantPicCntPresentFlag = br.readBits(1);
    info.valid = !br.overrun();
    return info;
}

// ---------- GOP 统计 ----------

GopStats computeGopStats(const QList<FrameType> &frames)
{
    GopStats s;
    s.totalFrames = frames.size();
    for (int i = 0; i < frames.size(); ++i) {
        switch (frames[i]) {
        case FrameType::Idr:
            s.keyframeIndices.append(i);
            ++s.iFrames;
            break;
        case FrameType::I:
            ++s.iFrames;
            break;
        case FrameType::P:
            ++s.pFrames;
            break;
        case FrameType::B:
            ++s.bFrames;
            break;
        default:
            ++s.unknownFrames;
            break;
        }
    }
    s.idrCount = s.keyframeIndices.size();
    if (s.idrCount >= 2) {
        bool first = true;
        qint64 sum = 0;
        for (int i = 1; i < s.keyframeIndices.size(); ++i) {
            const int gop = s.keyframeIndices[i] - s.keyframeIndices[i - 1];
            if (first) {
                s.minGop = gop;
                s.maxGop = gop;
                first = false;
            } else {
                s.minGop = qMin(s.minGop, gop);
                s.maxGop = qMax(s.maxGop, gop);
            }
            sum += gop;
        }
        s.avgGop = double(sum) / double(s.idrCount - 1);
    }
    return s;
}

} // namespace H264Parser
