/*
 *  Created on: Oct 3, 2019

	Copyright 2019 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "pipeline.h"
#include "hw/pvr/Renderer_if.h"

static const vk::CompareOp depthOps[] =
{
	vk::CompareOp::eNever,          //0 Never
	vk::CompareOp::eLess,           //1 Less
	vk::CompareOp::eEqual,          //2 Equal
	vk::CompareOp::eLessOrEqual,    //3 Less Or Equal
	vk::CompareOp::eGreater,        //4 Greater
	vk::CompareOp::eNotEqual,       //5 Not Equal
	vk::CompareOp::eGreaterOrEqual, //6 Greater Or Equal
	vk::CompareOp::eAlways,         //7 Always
};

static vk::BlendFactor getBlendFactor(u32 instr, bool src)
{
	switch (instr) {
	case 0:	// zero
		return vk::BlendFactor::eZero;
	case 1: // one
		return vk::BlendFactor::eOne;
	case 2: // other color
		return src ? vk::BlendFactor::eDstColor : vk::BlendFactor::eSrcColor;
	case 3: // inverse other color
		return src ? vk::BlendFactor::eOneMinusDstColor : vk::BlendFactor::eOneMinusSrcColor;
	case 4: // src alpha
		return vk::BlendFactor::eSrcAlpha;
	case 5: // inverse src alpha
		return vk::BlendFactor::eOneMinusSrcAlpha;
	case 6: // dst alpha
		return vk::BlendFactor::eDstAlpha;
	case 7: // inverse dst alpha
		return vk::BlendFactor::eOneMinusDstAlpha;
	default:
		die("Unsupported blend instruction");
		return vk::BlendFactor::eZero;
	}
}

void PipelineManager::CreatePipeline(u32 listType, bool sortTriangles, const PolyParam& pp)
{
	// Vertex input state
	const vk::VertexInputBindingDescription vertexBindingDescriptions[] =
	{
			{ 0, sizeof(Vertex) },
	};
	const vk::VertexInputAttributeDescription vertexInputAttributeDescriptions[] =
	{
			vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, x)),	// pos
			vk::VertexInputAttributeDescription(1, 0, vk::Format::eR8G8B8A8Uint, offsetof(Vertex, col)),	// base color
			vk::VertexInputAttributeDescription(2, 0, vk::Format::eR8G8B8A8Uint, offsetof(Vertex, spc)),	// offset color
			vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, u)),		// tex coord
	};
	vk::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo(
			vk::PipelineVertexInputStateCreateFlags(),
			ARRAY_SIZE(vertexBindingDescriptions),
			vertexBindingDescriptions,
			ARRAY_SIZE(vertexInputAttributeDescriptions),
			vertexInputAttributeDescriptions);

	// Input assembly state
	vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(vk::PipelineInputAssemblyStateCreateFlags(), vk::PrimitiveTopology::eTriangleStrip);

	// Viewport and scissor states
	vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo(vk::PipelineViewportStateCreateFlags(), 1, nullptr, 1, nullptr);

	// Rasterization and multisample states
	vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo
	(
	  vk::PipelineRasterizationStateCreateFlags(),  // flags
	  false,                                        // depthClampEnable
	  false,                                        // rasterizerDiscardEnable
	  vk::PolygonMode::eFill,                       // polygonMode
	  pp.isp.CullMode == 3 ? vk::CullModeFlagBits::eBack
			  : pp.isp.CullMode == 2 ? vk::CullModeFlagBits::eFront
			  : vk::CullModeFlagBits::eNone,        // cullMode
	  vk::FrontFace::eCounterClockwise,             // frontFace
	  false,                                        // depthBiasEnable
	  0.0f,                                         // depthBiasConstantFactor
	  0.0f,                                         // depthBiasClamp
	  0.0f,                                         // depthBiasSlopeFactor
	  1.0f                                          // lineWidth
	);
	vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo;

	// Depth and stencil
	vk::CompareOp depthOp;
	if (listType == ListType_Punch_Through || (listType == ListType_Translucent && sortTriangles))
		depthOp = vk::CompareOp::eGreaterOrEqual;
	else
		depthOp = depthOps[pp.isp.DepthMode];
	bool depthWriteEnable;
	if (sortTriangles && !settings.rend.PerStripSorting)
		depthWriteEnable = false;
	else
	{
		// Z Write Disable seems to be ignored for punch-through.
		// Fixes Worms World Party, Bust-a-Move 4 and Re-Volt
		if (listType == ListType_Punch_Through)
			depthWriteEnable = true;
		else
			depthWriteEnable = !pp.isp.ZWriteDis;
	}

	vk::StencilOpState stencilOpStateSet(vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::CompareOp::eAlways, 0, 0x80, 0x80);
	vk::StencilOpState stencilOpStateNop(vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::CompareOp::eAlways);
	vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo
	(
	  vk::PipelineDepthStencilStateCreateFlags(), // flags
	  true,                                       // depthTestEnable
	  depthWriteEnable,                           // depthWriteEnable
	  depthOp,                                    // depthCompareOp
	  false,                                      // depthBoundTestEnable
	  listType == ListType_Opaque || listType == ListType_Punch_Through, // stencilTestEnable
	  pp.pcw.Shadow != 0 ? stencilOpStateSet : stencilOpStateNop, // front
	  stencilOpStateNop                                           // back
	);

	// Color flags and blending
	vk::ColorComponentFlags colorComponentFlags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState;
	// Apparently punch-through polys support blending, or at least some combinations
	if (listType == ListType_Translucent || listType == ListType_Punch_Through)
	{
		u32 src = pp.tsp.SrcInstr;
		u32 dst = pp.tsp.DstInstr;
		pipelineColorBlendAttachmentState =
		{
		  true,                          // blendEnable
		  getBlendFactor(src, true),     // srcColorBlendFactor
		  getBlendFactor(dst, false),    // dstColorBlendFactor
		  vk::BlendOp::eAdd,             // colorBlendOp
		  getBlendFactor(src, true),     // srcAlphaBlendFactor
		  getBlendFactor(dst, false),    // dstAlphaBlendFactor
		  vk::BlendOp::eAdd,             // alphaBlendOp
		  colorComponentFlags            // colorWriteMask
		};
	}
	else
	{
		pipelineColorBlendAttachmentState =
		{
		  false,                      // blendEnable
		  vk::BlendFactor::eZero,     // srcColorBlendFactor
		  vk::BlendFactor::eZero,     // dstColorBlendFactor
		  vk::BlendOp::eAdd,          // colorBlendOp
		  vk::BlendFactor::eZero,     // srcAlphaBlendFactor
		  vk::BlendFactor::eZero,     // dstAlphaBlendFactor
		  vk::BlendOp::eAdd,          // alphaBlendOp
		  colorComponentFlags         // colorWriteMask
		};
	}

	vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo
	(
	  vk::PipelineColorBlendStateCreateFlags(),   // flags
	  false,                                      // logicOpEnable
	  vk::LogicOp::eNoOp,                         // logicOp
	  1,                                          // attachmentCount
	  &pipelineColorBlendAttachmentState,         // pAttachments
	  { { (1.0f, 1.0f, 1.0f, 1.0f) } }            // blendConstants
	);

	vk::DynamicState dynamicStates[2] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo(vk::PipelineDynamicStateCreateFlags(), 2, dynamicStates);

	vk::ShaderModule vertex_module = shaderManager.GetVertexShader(VertexShaderParams{ pp.pcw.Gouraud == 1, false });	// TODO rotate90
	FragmentShaderParams params = {};
	params.alphaTest = listType == ListType_Punch_Through;
	params.bumpmap = pp.tcw.PixelFmt == PixelBumpMap;
	params.clamping = pp.tsp.ColorClamp && (pvrrc.fog_clamp_min != 0 || pvrrc.fog_clamp_max != 0xffffffff);;
	switch (pp.tileclip >> 28)
	{
	case 2:
		params.clipTest = 1;	// render stuff inside the region
		break;
	case 3:
		params.clipTest = -1;	// render stuff outside the region
		break;
	default:
		params.clipTest = 0;	// always passes
		break;
	}
	params.fog = 2;							// TODO fog texture -> pp.tsp.FogCtrl;
	params.gouraud = pp.pcw.Gouraud;
	params.ignoreTexAlpha = pp.tsp.IgnoreTexA;
	params.offset = pp.pcw.Offset;
	params.shaderInstr = pp.tsp.ShadInstr;
	params.texture = pp.pcw.Texture;
	params.trilinear = pp.pcw.Texture && pp.tsp.FilterMode > 1 && listType != ListType_Punch_Through;
	params.useAlpha = pp.tsp.UseAlpha;
	vk::ShaderModule fragment_module = shaderManager.GetFragmentShader(params);

	vk::PipelineShaderStageCreateInfo stages[] = {
			{ vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eVertex, vertex_module, "main" },
			{ vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eFragment, fragment_module, "main" },
	};
	vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo
	(
	  vk::PipelineCreateFlags(),                  // flags
	  2,                                          // stageCount
	  stages,                                     // pStages
	  &pipelineVertexInputStateCreateInfo,        // pVertexInputState
	  &pipelineInputAssemblyStateCreateInfo,      // pInputAssemblyState
	  nullptr,                                    // pTessellationState
	  &pipelineViewportStateCreateInfo,           // pViewportState
	  &pipelineRasterizationStateCreateInfo,      // pRasterizationState
	  &pipelineMultisampleStateCreateInfo,        // pMultisampleState
	  &pipelineDepthStencilStateCreateInfo,       // pDepthStencilState
	  &pipelineColorBlendStateCreateInfo,         // pColorBlendState
	  &pipelineDynamicStateCreateInfo,            // pDynamicState
	  descriptorSets.GetPipelineLayout(),         // layout
	  GetContext()->GetRenderPass()             // renderPass
	);

	pipelines[hash(listType, sortTriangles, &pp)] = GetContext()->GetDevice()->createGraphicsPipelineUnique(GetContext()->GetPipelineCache(),
			graphicsPipelineCreateInfo);
}
