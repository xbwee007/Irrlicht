// Copyright (C) 2002-2009 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "IrrCompileConfig.h"
#ifdef _IRR_COMPILE_WITH_DIRECT3D_11_

#include "CD3D11NormalMapRenderer.h"
#include "IVideoDriver.h"
#include "IMaterialRendererServices.h"
#include "os.h"
#include "SLight.h"

#include <d3dCompiler.h>

namespace irr
{
namespace video  
{
	const char NORMAL_MAP_SHADER[] = 
		"// adding constant buffer for transform matrices\n"\
		"cbuffer cbPerFrame : register(c0)\n"\
		"{\n"\
		"   float4x4 g_mWorld;\n"\
		"   float4x4 g_mWorldViewProj;\n"\
		"	float3	 g_lightPos1;\n"\
		"	float4	 g_lightColor1;\n"\
		"	float3	 g_lightPos2;\n"\
		"	float4	 g_lightColor2;\n"\
		"};\n"\
		"\n"\
		"cbuffer cbConsts : register(c1)\n"\
		"{\n"\
		"	float3 zero = float3(0, 0, 0);\n"\
		"	float3 positiveHalf = float3(0.5f, 0.5f, 0.5f);\n"\
		"};\n"\
		"\n"\
		"// adding textures and samplers\n"\
		"Texture2D g_tex1 : register(t0);\n"\
		"Texture2D g_tex2 : register(t1);\n"\
		"SamplerState g_sampler1 : register(s0);\n"\
		"SamplerState g_sampler2 : register(s1);\n"\
		"\n"\
		"struct VS_INPUT\n"\
		"{\n"\
		"	float4 pos		: POSITION;\n"\
		"	float3 norm		: NORMAL;\n"\
		"	float4 color	: COLOR;\n"\
		"	float2 tex0		: TEXCOORD0;\n"\
		"	float3 tangent	: TEXCOORD1;\n"\
		"	float3 binormal : TEXCOORD2;\n"\
		"};\n"\
		"\n"\
		"struct PS_INPUT\n"\
		"{\n"\
		"	float4 pos				: SV_Position;\n"\
		"	float2 colorMapCoord	: TEXTURE0;\n"\
		"	float2 normalMapCoord	: TEXTURE1;\n"\
		"	float3 lightVector1		: TEXTURE2;\n"\
		"	float4 lightColor1		: COLOR0;\n"\
		"	float3 lightVector2		: TEXTURE3;\n"\
		"	float4 lightColor2		: COLOR1;\n"\
		"};\n"\
		"\n"\
		"PS_INPUT VS(VS_INPUT input)\n"\
		"{\n"\
		"	PS_INPUT output = (PS_INPUT)0;\n"\
		"\n"\
		"	// transform position to clip space\n"\
		"	output.pos = mul( input.pos, g_mWorldViewProj );\n"\
		"\n"\
		"	// transform normal, tangent and binormal\n"\
		"	float3x3 tbnMatrix = mul( float3x3( input.binormal, input.tangent , input.norm ), (float3x3)g_mWorld );\n"\
		"\n"\
		"	// transform vertex into world position\n"\
		"	float4 worldPos = mul( input.pos, g_mWorld );\n"\
		"\n"\
		"	float3 lightVec1 = g_lightPos1 - worldPos.xyz;\n"\
		"	float3 lightVec2 = g_lightPos2 - worldPos.xyz;\n"\
		"\n"\
		"	// transform light vectors with U, V, W\n"\
		"	output.lightVector1 = mul( tbnMatrix, lightVec1.xyz );\n"\
		"	output.lightVector2 = mul( tbnMatrix, lightVec2.xyz );\n"\
		"\n"\
		"	float tmp = dot( output.lightVector1, output.lightVector1 );\n"\
		"	tmp = rsqrt( tmp );\n"\
		"	output.lightVector1 = mul( output.lightVector1, tmp );\n"\
		"\n"\
		"	tmp = dot( output.lightVector2, output.lightVector2 );\n"\
		"	tmp = rsqrt( tmp );\n"\
		"	output.lightVector2 = mul( output.lightVector2, tmp );\n"\
		"\n"\
		"	// move light vectors from -1..1 into 0..1\n"\
		"	output.lightVector1 = mad( output.lightVector1, positiveHalf, positiveHalf );\n"\
		"	output.lightVector2 = mad( output.lightVector2, positiveHalf, positiveHalf );\n"\
		"\n"\
		"	// calculate attenuation of lights\n"
		"	lightVec1.x = mul( dot( lightVec1 , lightVec1 ), g_lightColor1.w );\n"\
		"	lightVec1 = rsqrt( lightVec1.x );\n"\
		"	output.lightColor1 = mul( lightVec1, g_lightColor1.xyz );\n"\
		"\n"\
		"	lightVec2.x = mul( dot( lightVec2, lightVec2 ), g_lightColor2.w );\n"\
		"	lightVec2 = rsqrt( lightVec2.x );\n"\
		"	output.lightColor2 = mul( lightVec2, g_lightColor2.xyz );\n"\
		"\n"\
		"	// output texture coordinates\n"\
		"	output.colorMapCoord = input.tex0;\n"\
		"	output.normalMapCoord = input.tex0;\n"\
		"	output.lightColor1.a = input.color.a;\n"\
		"\n"\
		"	return output;\n"\
		"}\n"\
		"\n"\
		"// High-definition pixel-shader\n"\
		"float4 PS(PS_INPUT input) : SV_Target\n"\
		"{\n"\
		"	// sample textures\n"\
		"	float4 colorMap = g_tex1.Sample( g_sampler1, input.colorMapCoord );\n"\
		"	float4 normalMap = g_tex2.Sample( g_sampler2, input.normalMapCoord );\n"\
		"\n"\
		"	normalMap = normalize(normalMap - float4( positiveHalf, 0.5f ) );\n"\
		"	float3 lightVec1 = normalize( input.lightVector1 - positiveHalf );\n"\
		"	float3 lightVec2 = normalize( input.lightVector2 - positiveHalf );\n"\
		"\n"\
		"	lightVec1 = dot( lightVec1, normalMap.xyz );\n"\
		"	lightVec1 = max( lightVec1, zero );\n"\
		"	lightVec1 = mul( lightVec1, input.lightColor1.xyz );\n"\
		"\n"\
		"	lightVec2 = dot( lightVec2, normalMap.xyz );\n"\
		"	lightVec2 = max( lightVec2, zero );\n"\
		"\n"\
		"	float4 finalColor = 0;\n"\
		"	finalColor.xyz = mad( lightVec2, input.lightColor2.xyz, lightVec1 );\n"\
		"	finalColor = mul( finalColor, colorMap );\n"\
		"	finalColor.w = input.lightColor1.w;\n"\
		"\n"\
		"	return finalColor;\n"\
		"}\n"\
		"\n"\
		"// Technique for standard vertex type\n"\
		"technique11 NormalMapTechnique\n"\
		"{\n"\
		"	pass p0\n"\
		"	{\n"\
		"		SetVertexShader( CompileShader( vs_4_0, VS() ) );\n"\
		"		SetGeometryShader( NULL );\n"\
		"		SetPixelShader( CompileShader( ps_4_0, PS() ) );\n"\
		"	}\n"\
		"}\n";

	CD3D11NormalMapRenderer::CD3D11NormalMapRenderer(ID3D11Device* device, video::IVideoDriver* driver, s32& outMaterialTypeNr, IMaterialRenderer* baseMaterial)
		: CD3D11ShaderMaterialRenderer(device, driver, 0, baseMaterial), 
		Effect(0), Technique(0), 
		WorldMatrix(0), WorldViewProjMatrix(0), 
		LightPos1(0), LightColor1(0), LightPos2(0), LightColor2(0)
	{
#ifdef _DEBUG
		setDebugName("CD3D11NormalMapRenderer");
#endif
		// set this as callback. We could have done this in 
		// the initialization list, but some compilers don't like it.

		CallBack = this;

		HRESULT hr = S_OK;
		ZeroMemory(&PassDescription, sizeof(D3DX11_PASS_DESC));

		video::IMaterialRenderer* renderer = driver->getMaterialRenderer(EMT_NORMAL_MAP_SOLID);
		if(renderer)
		{
			// Reuse effect if already exists
			Effect = ((video::CD3D11NormalMapRenderer*)renderer)->Effect;
			if(Effect)
				Effect->AddRef();
		}
		else
		{
			if(!init(NORMAL_MAP_SHADER))
				return;
		}

		if(Effect)
		{
			Technique = Effect->GetTechniqueByName("NormalMapTechnique");
			Technique->GetPassByIndex(0)->GetDesc(&PassDescription);
			WorldMatrix = Effect->GetVariableByName("g_mWorld")->AsMatrix();
			WorldViewProjMatrix = Effect->GetVariableByName("g_mWorldViewProj")->AsMatrix();
			LightPos1 = Effect->GetVariableByName("g_lightPos1")->AsVector();
			LightColor1 = Effect->GetVariableByName("g_lightColor1")->AsVector();
			LightPos2 = Effect->GetVariableByName("g_lightPos2")->AsVector();
			LightColor2 = Effect->GetVariableByName("g_lightColor2")->AsVector();

			outMaterialTypeNr = Driver->addMaterialRenderer(this);
		}
	}

	CD3D11NormalMapRenderer::~CD3D11NormalMapRenderer()
	{
		if(Effect)
			Effect->Release();

		if(CallBack == this)
			CallBack = NULL;
	}

	bool CD3D11NormalMapRenderer::OnRender(IMaterialRendererServices* service, E_VERTEX_TYPE vtxtype)
	{
		if (vtxtype != video::EVT_TANGENTS)
		{
			os::Printer::log("Error: Normal map renderer only supports vertices of type EVT_TANGENTS", ELL_ERROR);
			return false;
		}

		return CD3D11ShaderMaterialRenderer::OnRender(service, vtxtype);
	}

	//! Returns the render capability of the material.
	s32 CD3D11NormalMapRenderer::getRenderCapability() const
	{
		if (Driver->queryFeature(video::EVDF_PIXEL_SHADER_4_0) &&
			Driver->queryFeature(video::EVDF_VERTEX_SHADER_4_0))
			return 0;

		return 1;
	}

	void CD3D11NormalMapRenderer::OnSetConstants( IMaterialRendererServices* services, s32 userData )
	{
		// Set matrices
		WorldMatrix->SetMatrix((float*)Driver->getTransform(video::ETS_WORLD).pointer());

		core::matrix4 mat = Driver->getTransform(video::ETS_PROJECTION);
		mat *= Driver->getTransform(video::ETS_VIEW);
		mat *= Driver->getTransform(video::ETS_WORLD);
		WorldViewProjMatrix->SetMatrix((float*)mat.pointer());

		// here we've got to fetch the fixed function lights from the
		// driver and set them as constants
		u32 cnt = Driver->getDynamicLightCount();

		SLight light;

		if(cnt >= 1)
			light = Driver->getDynamicLight(0);	
		else
		{
			light.DiffuseColor.set(0,0,0); // make light dark
			light.Radius = 1.0f;
		}

		light.DiffuseColor.a = 1.0f/(light.Radius*light.Radius); // set attenuation

		LightPos1->SetFloatVector(reinterpret_cast<float*>(&light.Position));
		LightColor1->SetFloatVector(reinterpret_cast<float*>(&light.DiffuseColor));

		if(cnt >= 2)
			light = Driver->getDynamicLight(1);	
		else
		{
			light = SLight();
			light.DiffuseColor.set(0,0,0); // make light dark
			light.Radius = 1.0f;
		}

		light.DiffuseColor.a = 1.0f/(light.Radius*light.Radius); // set attenuation

		LightPos2->SetFloatVector(reinterpret_cast<float*>(&light.Position));
		LightColor2->SetFloatVector(reinterpret_cast<float*>(&light.DiffuseColor));

		// Apply effect
		Technique->GetPassByIndex(0)->Apply(0, Context );
	}

	void* CD3D11NormalMapRenderer::getShaderByteCode() const
	{
		return PassDescription.pIAInputSignature;
	}

	irr::u32 CD3D11NormalMapRenderer::getShaderByteCodeSize() const
	{
		return PassDescription.IAInputSignatureSize;
	}

	bool CD3D11NormalMapRenderer::init( const char* shader )
	{
		// Create effect if this is first
		UINT flags = 0;
		//flags |= D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;
#ifdef _DEBUG
		// These values allow use of PIX and shader debuggers
		flags |= D3D10_SHADER_DEBUG;
		flags |= D3D10_SHADER_SKIP_OPTIMIZATION;
#else
		// These flags allow maximum performance
		flags |= D3D10_SHADER_ENABLE_STRICTNESS;
		flags |= D3D10_SHADER_OPTIMIZATION_LEVEL3;
#endif
		ID3DBlob* ppCode = NULL;
		ID3DBlob* ppErrors = NULL;

		HRESULT hr = D3DCompile(NORMAL_MAP_SHADER, strlen(NORMAL_MAP_SHADER), "", NULL, NULL, NULL, "fx_5_0", flags, 2, &ppCode, &ppErrors );
		if (FAILED(hr))
		{
			core::stringc errorMsg = "Error, could not compile normal map effect";
			if (ppErrors)
			{
				errorMsg += ": ";
				errorMsg += static_cast<const char*>(ppErrors->GetBufferPointer());
				ppErrors->Release();
			}
			os::Printer::log(errorMsg.c_str(), ELL_ERROR);
			return false;
		}

		hr = D3DX11CreateEffectFromMemory( ppCode->GetBufferPointer(), ppCode->GetBufferSize(), flags, Device, &Effect );

		if (FAILED(hr))
		{
			os::Printer::log("Error, could not create normal map effect", ELL_ERROR);
			return false;
		}

		return true;
	}

	void CD3D11NormalMapRenderer::OnSetMaterial( const SMaterial& material )
	{
		CurrentMaterial = material;
	}

}
}

#endif