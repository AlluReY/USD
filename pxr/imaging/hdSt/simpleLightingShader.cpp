//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/hdSt/simpleLightingShader.h"
#include "pxr/imaging/hdSt/textureResource.h"
#include "pxr/imaging/hdSt/package.h"
#include "pxr/imaging/hdSt/materialParam.h"
#include "pxr/imaging/hdSt/resourceBinder.h"

#include "pxr/imaging/hd/binding.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/imaging/hf/perfLog.h"

#include "pxr/imaging/hio/glslfx.h"

#include "pxr/imaging/glf/bindingMap.h"
#include "pxr/imaging/glf/simpleLightingContext.h"

#include "pxr/base/tf/staticTokens.h"

#include <boost/functional/hash.hpp>

#include <string>
#include <sstream>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (domeLightIrradiance)
    (domeLightPrefilter) 
    (domeLightBRDF)
);


HdStSimpleLightingShader::HdStSimpleLightingShader() 
    : _lightingContext(GlfSimpleLightingContext::New())
    , _bindingMap(TfCreateRefPtr(new GlfBindingMap()))
    , _useLighting(true)
    , _glslfx(new HioGlslfx(HdStPackageSimpleLightingShader()))
{
    _lightingContext->InitUniformBlockBindings(_bindingMap);
    _lightingContext->InitSamplerUnitBindings(_bindingMap);

}

HdStSimpleLightingShader::~HdStSimpleLightingShader() = default;

/* virtual */
HdStSimpleLightingShader::ID
HdStSimpleLightingShader::ComputeHash() const
{
    HD_TRACE_FUNCTION();

    TfToken glslfxFile = HdStPackageSimpleLightingShader();
    size_t numLights = _useLighting ? _lightingContext->GetNumLightsUsed() : 0;
    bool useShadows = _useLighting ? _lightingContext->GetUseShadows() : false;
    size_t numShadows = useShadows ? _lightingContext->ComputeNumShadowsUsed() : 0;

    size_t hash = glslfxFile.Hash();
    boost::hash_combine(hash, numLights);
    boost::hash_combine(hash, useShadows);
    boost::hash_combine(hash, numShadows);

    return (ID)hash;
}

/* virtual */
std::string
HdStSimpleLightingShader::GetSource(TfToken const &shaderStageKey) const
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    std::string source = _glslfx->GetSource(shaderStageKey);

    if (source.empty()) return source;

    std::stringstream defineStream;
    size_t numLights = _useLighting ? _lightingContext->GetNumLightsUsed() : 0;
    bool useShadows = _useLighting ? _lightingContext->GetUseShadows() : false;
    size_t numShadows = useShadows ? _lightingContext->ComputeNumShadowsUsed() : 0;
    defineStream << "#define NUM_LIGHTS " << numLights<< "\n";
    defineStream << "#define USE_SHADOWS " << (int)(useShadows) << "\n";
    defineStream << "#define NUM_SHADOWS " << numShadows << "\n";
    if (useShadows) {
        bool const useBindlessShadowMaps =
            GlfSimpleShadowArray::GetBindlessShadowMapsEnabled();;
        defineStream << "#define USE_BINDLESS_SHADOW_TEXTURES "
                     << int(useBindlessShadowMaps) << "\n";
    }

    return defineStream.str() + source;
}

/* virtual */
void
HdStSimpleLightingShader::SetCamera(GfMatrix4d const &worldToViewMatrix,
                                          GfMatrix4d const &projectionMatrix)
{
    _lightingContext->SetCamera(worldToViewMatrix, projectionMatrix);
}

/* virtual */
void
HdStSimpleLightingShader::BindResources(const int program,
                                       HdSt_ResourceBinder const &binder,
                                       HdRenderPassState const &state)
{
    // XXX: we'd like to use HdSt_ResourceBinder instead of GlfBindingMap.
    //
    _bindingMap->AssignUniformBindingsToProgram(program);
    _lightingContext->BindUniformBlocks(_bindingMap);

    _bindingMap->AssignSamplerUnitsToProgram(program);
    _lightingContext->BindSamplers(_bindingMap);

    for (auto const& light : _lightingContext->GetLights()){

        if (light.IsDomeLight()) {

            HdBinding irradianceBinding = 
                                binder.GetBinding(_tokens->domeLightIrradiance);
            if (irradianceBinding.GetType() == HdBinding::TEXTURE_2D) {
                int samplerUnit = irradianceBinding.GetTextureUnit();
                
                uint32_t textureId = uint32_t(light.GetIrradianceId());
                
                glActiveTexture(GL_TEXTURE0 + samplerUnit);
                glBindTexture(GL_TEXTURE_2D, (GLuint)textureId);
                glBindSampler(samplerUnit, 0);
            } 
            HdBinding prefilterBinding = 
                                binder.GetBinding(_tokens->domeLightPrefilter);
            if (prefilterBinding.GetType() == HdBinding::TEXTURE_2D) {
                int samplerUnit = prefilterBinding.GetTextureUnit();
                
                uint32_t textureId = uint32_t(light.GetPrefilterId());
                
                glActiveTexture(GL_TEXTURE0 + samplerUnit);
                glBindTexture(GL_TEXTURE_2D, (GLuint)textureId); 
                glBindSampler(samplerUnit, 0);
            } 
            HdBinding brdfBinding = binder.GetBinding(_tokens->domeLightBRDF);
            if (brdfBinding.GetType() == HdBinding::TEXTURE_2D) {
                int samplerUnit = brdfBinding.GetTextureUnit();
                
                uint32_t textureId = uint32_t(light.GetBrdfId());
                
                glActiveTexture(GL_TEXTURE0 + samplerUnit);
                glBindTexture(GL_TEXTURE_2D, (GLuint)textureId);
                glBindSampler(samplerUnit, 0);
            }
        }
    }
    glActiveTexture(GL_TEXTURE0);
    binder.BindShaderResources(this);
}

/* virtual */
void
HdStSimpleLightingShader::UnbindResources(const int program,
                                         HdSt_ResourceBinder const &binder,
                                         HdRenderPassState const &state)
{
    // XXX: we'd like to use HdSt_ResourceBinder instead of GlfBindingMap.
    //
    _lightingContext->UnbindSamplers(_bindingMap);

    for (auto const& light : _lightingContext->GetLights()){

        if (light.IsDomeLight()) {

            HdBinding irradianceBinding = 
                                binder.GetBinding(_tokens->domeLightIrradiance);
            if (irradianceBinding.GetType() == HdBinding::TEXTURE_2D) {
                int samplerUnit = irradianceBinding.GetTextureUnit();
                glActiveTexture(GL_TEXTURE0 + samplerUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
                
                glBindSampler(samplerUnit, 0);
            } 
            HdBinding prefilterBinding = 
                                binder.GetBinding(_tokens->domeLightPrefilter);
            if (prefilterBinding.GetType() == HdBinding::TEXTURE_2D) {
                int samplerUnit = prefilterBinding.GetTextureUnit();
                glActiveTexture(GL_TEXTURE0 + samplerUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
                
                glBindSampler(samplerUnit, 0);
            } 
            HdBinding brdfBinding = binder.GetBinding(_tokens->domeLightBRDF);
            if (brdfBinding.GetType() == HdBinding::TEXTURE_2D) {
                int samplerUnit = brdfBinding.GetTextureUnit();
                glActiveTexture(GL_TEXTURE0 + samplerUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
                
                glBindSampler(samplerUnit, 0);
            }
        }
    }
    glActiveTexture(GL_TEXTURE0);
}

/*virtual*/
void
HdStSimpleLightingShader::AddBindings(HdBindingRequestVector *customBindings)
{
    _lightTextureParams.clear();

    bool haveDomeLight = false;
    for (auto const& light : _lightingContext->GetLights()) { 

        if (light.IsDomeLight() && !haveDomeLight) {

            // For now we assume that the only simple light with a texture is
            // a domeLight (ignoring RectLights, and multiple domeLights)
            haveDomeLight = true;

            // irradiance map
            _lightTextureParams.push_back(
                    HdSt_MaterialParam(HdSt_MaterialParam::ParamTypeTexture,
                    _tokens->domeLightIrradiance,
                    VtValue(GfVec4f(0.0)),
                    SdfPath(),
                    TfTokenVector(),
                    HdTextureType::Uv));
            // prefilter map
            _lightTextureParams.push_back(
                    HdSt_MaterialParam(HdSt_MaterialParam::ParamTypeTexture,
                    _tokens->domeLightPrefilter,
                    VtValue(GfVec4f(0.0)),
                    SdfPath(),
                    TfTokenVector(),
                    HdTextureType::Uv));
            // BRDF texture
            _lightTextureParams.push_back(
                    HdSt_MaterialParam(HdSt_MaterialParam::ParamTypeTexture,
                    _tokens->domeLightBRDF,
                    VtValue(GfVec4f(0.0)),
                    SdfPath(),
                    TfTokenVector(),
                    HdTextureType::Uv));
        }
    }
}

HdSt_MaterialParamVector const& 
HdStSimpleLightingShader::GetParams() const 
{
    return _lightTextureParams;
}

void
HdStSimpleLightingShader::SetLightingStateFromOpenGL()
{
    _lightingContext->SetStateFromOpenGL();
}

void
HdStSimpleLightingShader::SetLightingState(
    GlfSimpleLightingContextPtr const &src)
{
    if (src) {
        _useLighting = true;
        _lightingContext->SetUseLighting(!src->GetLights().empty());
        _lightingContext->SetLights(src->GetLights());
        _lightingContext->SetMaterial(src->GetMaterial());
        _lightingContext->SetSceneAmbient(src->GetSceneAmbient());
        _lightingContext->SetShadows(src->GetShadows());
    } else {
        // XXX:
        // if src is null, turn off lights (this is temporary used for shadowmap drawing).
        // see GprimUsdBaseIcBatch::Draw()
        _useLighting = false;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

