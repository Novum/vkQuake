// MIT License
//
// Copyright (c) 2016-2021 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

layout (push_constant) uniform constants
{
	vec4 viewport, projection, settings;
}
pc;

struct gtao_constants
{
	ivec2 viewport_size;
	vec2  viewport_pixel_size;

	vec2 ndc_to_view_mul;
	vec2 ndc_to_view_add;

	vec2  ndc_to_view_mul_x_pixel_size;
	float effect_radius;
	float effect_falloff_range;

	float final_value_power;
	float denoise_blur_beta;

	float depth_mip_sampling_offset;
};

gtao_constants get_gtao_constants ()
{
	gtao_constants c;
	c.viewport_size = ivec2 (pc.viewport.xy);
	c.viewport_pixel_size = 1.0 / pc.viewport.xy;
	c.ndc_to_view_mul = pc.viewport.zw;
	c.ndc_to_view_add = pc.projection.xy;
	c.ndc_to_view_mul_x_pixel_size = c.ndc_to_view_mul * c.viewport_pixel_size;
	c.effect_radius = pc.settings.x;
	c.effect_falloff_range = 0.615;
	c.final_value_power = 2.2;
	c.denoise_blur_beta = 1.2;
	c.depth_mip_sampling_offset = 3.3;
	return c;
}
