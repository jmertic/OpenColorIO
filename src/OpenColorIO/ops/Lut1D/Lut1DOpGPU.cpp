// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenColorIO Project.

#include <algorithm>
#include <iterator>

#include <OpenColorIO/OpenColorIO.h>

#include "GpuShaderUtils.h"
#include "MathUtils.h"
#include "ops/Lut1D/Lut1DOpGPU.h"

OCIO_NAMESPACE_ENTER
{

namespace
{
void PadLutChannels(unsigned long width,
                    unsigned long height,
                    const std::vector<float> & channel,
                    std::vector<float> & chn)
{
    const unsigned long currWidth = (unsigned long)(channel.size() / 3);

    if (height>1)
    {
        // Fill the texture values.
        //
        // Make the last texel of a given row the same as the first texel 
        // of its next row.  This will preserve the continuity along row breaks 
        // as long as the lookup position used by the sampler is based on (width-1) 
        // to account for the 1 texel padding at the end of each row.
        unsigned long leftover = currWidth;

        const unsigned long step = width - 1;
        for (unsigned long i = 0; i < (currWidth - step); i += step)
        {
            std::transform(&channel[3 * i],
                           &channel[3 * (i + step)],
                           std::back_inserter(chn),
                           [](float val) {return SanitizeFloat(val); });

            chn.push_back(SanitizeFloat(channel[3 * (i + step) + 0]));
            chn.push_back(SanitizeFloat(channel[3 * (i + step) + 1]));
            chn.push_back(SanitizeFloat(channel[3 * (i + step) + 2]));
            leftover -= step;
        }

        // If there are still texels to fill, add them to the texture data.
        if (leftover > 0)
        {
            std::transform(&channel[3 * (currWidth - leftover)],
                           &channel[3 * (currWidth - 1)],
                           std::back_inserter(chn),
                           [](float val) {return SanitizeFloat(val); });

            chn.push_back(SanitizeFloat(channel[3 * (currWidth - 1) + 0]));
            chn.push_back(SanitizeFloat(channel[3 * (currWidth - 1) + 1]));
            chn.push_back(SanitizeFloat(channel[3 * (currWidth - 1) + 2]));
        }
    }
    else
    {
        for (auto & val : channel)
        {
            chn.push_back(SanitizeFloat(val));
        }
    }

    // Pad the remaining of the texture with the last LUT entry.
    // Note: GPU Textures are expected a size of width*height.

    unsigned long missingEntries = width*height - ((unsigned long)chn.size() / 3);
    for (unsigned long idx = 0; idx < missingEntries; ++idx)
    {
        chn.push_back(SanitizeFloat(channel[3 * (currWidth - 1) + 0]));
        chn.push_back(SanitizeFloat(channel[3 * (currWidth - 1) + 1]));
        chn.push_back(SanitizeFloat(channel[3 * (currWidth - 1) + 2]));
    }
}
}

void GetLut1DGPUShaderProgram(GpuShaderDescRcPtr & shaderDesc,
                              ConstLut1DOpDataRcPtr & lutData)
{
    const unsigned long defaultMaxWidth = shaderDesc->getTextureMaxWidth();

    const unsigned long length = lutData->getArray().getLength();
    const unsigned long width = std::min(length, defaultMaxWidth);
    const unsigned long height = (length / defaultMaxWidth) + 1;

    // Adjust LUT texture to allow for correct 2d linear interpolation, if needed.

    std::vector<float> values;
    values.reserve(width*height*3);

    PadLutChannels(width, height, lutData->getArray().getValues(), values);

    // Register the RGB LUT.

    std::ostringstream resName;
    resName << shaderDesc->getResourcePrefix()
            << std::string("lut1d_")
            << shaderDesc->getNumTextures();

    const std::string name(resName.str());
    
    shaderDesc->addTexture(GpuShaderText::getSamplerName(name).c_str(),
                           lutData->getCacheID().c_str(),
                           width, height,
                           GpuShaderDesc::TEXTURE_RGB_CHANNEL,
                           lutData->getConcreteInterpolation(),
                           &values[0]);

    // Add the LUT code to the OCIO shader program.

    if (height > 1 || lutData->isInputHalfDomain())
    {
        // In case the 1D LUT length exceeds the 1D texture maximum length
        // a 2D texture is used.

        {
            GpuShaderText ss(shaderDesc->getLanguage());
            ss.declareTex2D(name);
            shaderDesc->addToDeclareShaderCode(ss.string().c_str());
        }

        {
            GpuShaderText ss(shaderDesc->getLanguage());

            ss.newLine() << ss.vec2fKeyword() << " " << name << "_computePos(float f)";
            ss.newLine() << "{";
            ss.indent();

            if (lutData->isInputHalfDomain())
            {
                static const float NEG_MIN_EXP = 15.0f;
                static const float EXP_SCALE = 1024.0f;
                static const float HALF_DENRM_MAX = 6.09755515e-05f;  // e.g. 2^-14 - 2^-24

                ss.newLine() << "float dep;";
                ss.newLine() << "float abs_f = abs(f);";
                ss.newLine() << "if (abs_f > " << (float)HALF_NRM_MIN << ")";
                ss.newLine() << "{";
                ss.indent();
                ss.declareVec3f("fComp", NEG_MIN_EXP, NEG_MIN_EXP, NEG_MIN_EXP);
                ss.newLine() << "float absarr = min( abs_f, " << (float)HALF_MAX << ");";
                // Compute the exponent, scaled [-14,15].
                ss.newLine() << "fComp.x = floor( log2( absarr ) );";
                // Lower is the greatest power of 2 <= f.
                ss.newLine() << "float lower = pow( 2.0, fComp.x );";
                // Compute the mantissa (scaled [0-1]).
                ss.newLine() << "fComp.y = ( absarr - lower ) / lower;";
                // The dot product recombines the parts into a raw half without the sign component:
                //   dep = [ exponent + mantissa + NEG_MIN_EXP] * scale
                ss.declareVec3f("scale", EXP_SCALE, EXP_SCALE, EXP_SCALE);
                ss.newLine() << "dep = dot( fComp, scale );";
                ss.dedent();
                ss.newLine() << "}";
                ss.newLine() << "else";
                ss.newLine() << "{";
                ss.indent();
                // Extract bits from denormalized values
                ss.newLine() << "dep = abs_f * 1023.0 / " << HALF_DENRM_MAX << ";";
                ss.dedent();
                ss.newLine() << "}";

                // Adjust position for negative values
                ss.newLine() << "dep += step(f, 0.0) * 32768.0;";

                // At this point 'dep' contains the raw half
                // Note: Raw halfs for NaN floats cannot be computed using
                //       floating-point operations.
                ss.newLine() << ss.vec2fDecl("retVal") << ";";
                ss.newLine() << "retVal.y = floor(dep / " << float(width - 1) << ");";       // floor( dep / (width-1) ))
                ss.newLine() << "retVal.x = dep - retVal.y * " << float(width - 1) << ";";   // dep - retVal.y * (width-1)

                ss.newLine() << "retVal.x = (retVal.x + 0.5) / " << float(width) << ";";   // (retVal.x + 0.5) / width;
                ss.newLine() << "retVal.y = (retVal.y + 0.5) / " << float(height) << ";";  // (retVal.x + 0.5) / height;
            }
            else
            {
                // Need min() to protect against f > 1 causing a bogus x value.
                // min( f, 1.) * (dim - 1)
                ss.newLine() << "float dep = min(f, 1.0) * " << float(length - 1) << ";";

                ss.newLine() << ss.vec2fDecl("retVal") << ";";
                // float(int( dep / (width-1) ))
                ss.newLine() << "retVal.y = float(int(dep / " << float(width - 1) << "));";
                // dep - retVal.y * (width-1)
                ss.newLine() << "retVal.x = dep - retVal.y * " << float(width - 1) << ";";

                // (retVal.x + 0.5) / width;
                ss.newLine() << "retVal.x = (retVal.x + 0.5) / " << float(width) << ";";
                // (retVal.x + 0.5) / height;
                ss.newLine() << "retVal.y = (retVal.y + 0.5) / " << float(height) << ";";
            }

            ss.newLine() << "return retVal;";
            ss.dedent();
            ss.newLine() << "}";

            shaderDesc->addToHelperShaderCode(ss.string().c_str());
        }
    }
    else
    {
        GpuShaderText ss(shaderDesc->getLanguage());
        ss.declareTex1D(name);
        shaderDesc->addToDeclareShaderCode(ss.string().c_str());
    }

    GpuShaderText ss(shaderDesc->getLanguage());
    ss.indent();

    ss.newLine() << "";
    ss.newLine() << "// Add a LUT 1D processing for " << name;
    ss.newLine() << "";

    ss.newLine() << "{";
    ss.indent();

    if (lutData->getHueAdjust() == HUE_DW3)
    {
        ss.newLine() << "// Add the pre hue adjustment";
        ss.newLine() << ss.vec3fDecl("maxval")
                        << " = max(" << shaderDesc->getPixelName() << ".rgb, max("
                        << shaderDesc->getPixelName() << ".gbr, " << shaderDesc->getPixelName() << ".brg));";
        ss.newLine() << ss.vec3fDecl("minval")
                        << " = min(" << shaderDesc->getPixelName() << ".rgb, min("
                        << shaderDesc->getPixelName() << ".gbr, " << shaderDesc->getPixelName() << ".brg));";
        ss.newLine() << "float oldChroma = max(1e-8, maxval.r - minval.r);";
        ss.newLine() << ss.vec3fDecl("delta") << " = " << shaderDesc->getPixelName() << ".rgb - minval;";
        ss.newLine() << "";
    }

    if (height > 1 || lutData->isInputHalfDomain())
    {
        const std::string str = name + "_computePos(" + shaderDesc->getPixelName();

        ss.newLine() << shaderDesc->getPixelName() << ".r = " << ss.sampleTex2D(name, str + ".r)") << ".r;";
        ss.newLine() << shaderDesc->getPixelName() << ".g = " << ss.sampleTex2D(name, str + ".g)") << ".g;";
        ss.newLine() << shaderDesc->getPixelName() << ".b = " << ss.sampleTex2D(name, str + ".b)") << ".b;";
    }
    else
    {
        const float dim = (float)lutData->getArray().getLength();

        ss.newLine() << ss.vec3fDecl(name + "_coords")
                        << " = (" << shaderDesc->getPixelName() << ".rgb * "
                        << ss.vec3fConst(dim - 1)
                        << " + " << ss.vec3fConst(0.5f) << " ) / "
                        << ss.vec3fConst(dim) << ";";

        ss.newLine() << shaderDesc->getPixelName() << ".r = "
                        << ss.sampleTex1D(name, name + "_coords.r") << ".r;";
        ss.newLine() << shaderDesc->getPixelName() << ".g = "
                        << ss.sampleTex1D(name, name + "_coords.g") << ".g;";
        ss.newLine() << shaderDesc->getPixelName() << ".b = "
                        << ss.sampleTex1D(name, name + "_coords.b") << ".b;";
    }

    if (lutData->getHueAdjust() == HUE_DW3)
    {
        ss.newLine() << "";
        ss.newLine() << "// Add the post hue adjustment";
        ss.newLine() << ss.vec3fDecl("maxval2") << " = max(" << shaderDesc->getPixelName()
                        << ".rgb, max(" << shaderDesc->getPixelName() << ".gbr, "
                        << shaderDesc->getPixelName() << ".brg));";
        ss.newLine() << ss.vec3fDecl("minval2") << " = min(" << shaderDesc->getPixelName()
                        << ".rgb, min(" << shaderDesc->getPixelName() << ".gbr, "
                        << shaderDesc->getPixelName() << ".brg));";
        ss.newLine() << "float newChroma = maxval2.r - minval2.r;";
        ss.newLine() << shaderDesc->getPixelName()
                        << ".rgb = minval2.r + delta * newChroma / oldChroma;";
    }

    ss.dedent();
    ss.newLine() << "}";

    shaderDesc->addToFunctionShaderCode(ss.string().c_str());

}


}
OCIO_NAMESPACE_EXIT

///////////////////////////////////////////////////////////////////////////////

#ifdef OCIO_UNIT_TEST

namespace OCIO = OCIO_NAMESPACE;
#include "UnitTest.h"

OCIO_ADD_TEST(Lut1DOp, pad_lut_one_dimension)
{
    const unsigned width = 6;

    // Create a channel multi row and smaller than the expected texture size.

    std::vector<float> channel;
    channel.resize((width - 2) * 3);

    // Fill the channel.

    for (unsigned idx = 0; idx<channel.size() / 3; ++idx)
    {
        channel[3*idx] = float(idx);
        channel[3*idx + 1] = float(idx) + 0.1f;
        channel[3*idx + 2] = float(idx) + 0.2f;
    }

    // Pad the texture values.

    std::vector<float> chn;
    OCIO_CHECK_NO_THROW(OCIO::PadLutChannels(width, 1, channel, chn));

    // Check the values.

    const float res[18] = { 0.0f, 0.1f, 0.2f, 1.0f, 1.1f, 1.2f,
                            2.0f, 2.1f, 2.2f, 3.0f, 3.1f, 3.2f,
                            3.0f, 3.1f, 3.2f, 3.0f, 3.1f, 3.2f };
    OCIO_CHECK_EQUAL(chn.size(), 18);
    for (unsigned idx = 0; idx<chn.size(); ++idx)
    {
        OCIO_CHECK_EQUAL(chn[idx], res[idx]);
    }
}

OCIO_ADD_TEST(Lut1DOp, pad_lut_two_dimension_1)
{
    const unsigned width = 4;
    const unsigned height = 3;

    std::vector<float> channel;
    channel.resize((height * width - 4) * 3);

    for (unsigned idx = 0; idx<channel.size() / 3; ++idx)
    {
        channel[3 * idx] = float(idx);
        channel[3 * idx + 1] = float(idx) + 0.1f;
        channel[3 * idx + 2] = float(idx) + 0.2f;
    }

    std::vector<float> chn;
    OCIO_CHECK_NO_THROW(OCIO::PadLutChannels(width, height, channel, chn));

    const float res[] = {
        0.0f, 0.1f, 0.2f, 1.0f, 1.1f, 1.2f, 2.0f, 2.1f, 2.2f, 3.0f, 3.1f, 3.2f,
        3.0f, 3.1f, 3.2f, 4.0f, 4.1f, 4.2f, 5.0f, 5.1f, 5.2f, 6.0f, 6.1f, 6.2f, 
        6.0f, 6.1f, 6.2f, 7.0f, 7.1f, 7.2f, 7.0f, 7.1f, 7.2f, 7.0f, 7.1f, 7.2f };
    OCIO_CHECK_EQUAL(chn.size(), 36);
    for (unsigned idx = 0; idx<chn.size(); ++idx)
    {
        OCIO_CHECK_EQUAL(chn[idx], res[idx]);
    }
}

OCIO_ADD_TEST(Lut1DOp, pad_lut_two_dimension_2)
{
    const unsigned width = 4;
    const unsigned height = 3;

    std::vector<float> channel;
    channel.resize((height * width - 3) * 3);

    for (unsigned idx = 0; idx<channel.size() / 3; ++idx)
    {
        channel[3 * idx] = float(idx);
        channel[3 * idx + 1] = float(idx) + 0.1f;
        channel[3 * idx + 2] = float(idx) + 0.2f;
    }

    // Special case where size%(width-1) = 0
    std::vector<float> chn;
    OCIO_CHECK_NO_THROW(OCIO::PadLutChannels(width, height, channel, chn));

    // Check the values

    const float res[] = {
        0.0f, 0.1f, 0.2f, 1.0f, 1.1f, 1.2f, 2.0f, 2.1f, 2.2f, 3.0f, 3.1f, 3.2f,
        3.0f, 3.1f, 3.2f, 4.0f, 4.1f, 4.2f, 5.0f, 5.1f, 5.2f, 6.0f, 6.1f, 6.2f,
        6.0f, 6.1f, 6.2f, 7.0f, 7.1f, 7.2f, 8.0f, 8.1f, 8.2f, 8.0f, 8.1f, 8.2f };
    
    OCIO_CHECK_EQUAL(chn.size(), 36);

    for (unsigned idx = 0; idx<chn.size(); ++idx)
    {
        OCIO_CHECK_EQUAL(chn[idx], res[idx]);
    }
}

#endif
