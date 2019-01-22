#include <vkhr/rasterizer/volume.hh>

#include <vkhr/rasterizer.hh>

#include <vkhr/scene_graph/camera.hh>
#include <vkhr/scene_graph/light_source.hh>

#include <vkpp/debug_marker.hh>

namespace vkhr {
    namespace vulkan {
        Volume::Volume(vkhr::Rasterizer& vulkan_renderer) {
            load(vulkan_renderer);
        }

        void Volume::load(vkhr::Rasterizer& vulkan_renderer) {
            auto cube_vertices = generate_cube_vertices();

            vertices = vk::VertexBuffer {
                vulkan_renderer.device,
                vulkan_renderer.command_pool,
                cube_vertices
            };

            vk::DebugMarker::object_name(vulkan_renderer.device, vertices, VK_OBJECT_TYPE_BUFFER, "Volume Vertex Buffer", id);
            vk::DebugMarker::object_name(vulkan_renderer.device, vertices.get_device_memory(), VK_OBJECT_TYPE_DEVICE_MEMORY,
                                         "Volume Vertex Device Memory", id);

            auto cube_elements = generate_cube_elements();

            elements = vk::IndexBuffer {
                vulkan_renderer.device,
                vulkan_renderer.command_pool,
                cube_elements
            };

            vk::DebugMarker::object_name(vulkan_renderer.device, elements, VK_OBJECT_TYPE_BUFFER, "Volume Index Buffer", id);
            vk::DebugMarker::object_name(vulkan_renderer.device, elements.get_device_memory(), VK_OBJECT_TYPE_DEVICE_MEMORY,
                                         "Volume Index Device Memory", id);

            ++id;
        }

        void Volume::set_current_volume(vk::ImageView& volume_view) {
            this->volume_view = &volume_view;
        }

        void Volume::set_parameter_buffer(vk::UniformBuffer& param) {
            this->parameter_buffer = &param;
        }

        void Volume::set_volume_sampler(vk::Sampler& voxel_sampler) {
            this->volume_sampler = &voxel_sampler;
        }

        std::vector<glm::vec3> Volume::generate_cube_vertices() const {
            std::vector<glm::vec3> cube_vertices(8);

            std::size_t v { 0 };

            cube_vertices[v++] = glm::vec3 { 0, 0, 0 };
            cube_vertices[v++] = glm::vec3 { 1, 0, 0 };
            cube_vertices[v++] = glm::vec3 { 1, 1, 0 };
            cube_vertices[v++] = glm::vec3 { 0, 1, 0 };

            cube_vertices[v++] = glm::vec3 { 0, 0, 1 };
            cube_vertices[v++] = glm::vec3 { 1, 0, 1 };
            cube_vertices[v++] = glm::vec3 { 1, 1, 1 };
            cube_vertices[v++] = glm::vec3 { 0, 1, 1 };

            return cube_vertices;
        }

        std::vector<unsigned>  Volume::generate_cube_elements() const {
            std::vector<unsigned> cube_elements(36);

            std::size_t e { 0 };

            cube_elements[e++] = 0; cube_elements[e++] = 1; cube_elements[e++] = 2;
            cube_elements[e++] = 2; cube_elements[e++] = 3; cube_elements[e++] = 0;

            cube_elements[e++] = 1; cube_elements[e++] = 5; cube_elements[e++] = 6;
            cube_elements[e++] = 6; cube_elements[e++] = 2; cube_elements[e++] = 1;

            cube_elements[e++] = 4; cube_elements[e++] = 0; cube_elements[e++] = 3;
            cube_elements[e++] = 3; cube_elements[e++] = 7; cube_elements[e++] = 4;

            cube_elements[e++] = 4; cube_elements[e++] = 5; cube_elements[e++] = 1;
            cube_elements[e++] = 1; cube_elements[e++] = 0; cube_elements[e++] = 4;

            cube_elements[e++] = 3; cube_elements[e++] = 2; cube_elements[e++] = 6;
            cube_elements[e++] = 6; cube_elements[e++] = 7; cube_elements[e++] = 3;

            cube_elements[e++] = 7; cube_elements[e++] = 6; cube_elements[e++] = 5;
            cube_elements[e++] = 5; cube_elements[e++] = 4; cube_elements[e++] = 7;

            return cube_elements;
        }

        void Volume::draw(Pipeline& pipeline, vk::DescriptorSet& descriptor_set, vk::CommandBuffer& command_buffer) {
            descriptor_set.write(2, *parameter_buffer);
            descriptor_set.write(3, *volume_view, *volume_sampler);
            command_buffer.bind_descriptor_set(descriptor_set, pipeline);
            command_buffer.bind_vertex_buffer(0, vertices, 0);
            command_buffer.bind_index_buffer(elements);
            command_buffer.draw_indexed(elements.count());
        }

        void Volume::build_pipeline(Pipeline& pipeline, Rasterizer& vulkan_renderer) {
            pipeline = Pipeline { /* In the case we are re-creating the pipeline. */ };

            pipeline.fixed_stages.add_vertex_binding({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3) });

            pipeline.fixed_stages.set_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

            pipeline.fixed_stages.set_scissor({ 0, 0, vulkan_renderer.swap_chain.get_extent() });
            pipeline.fixed_stages.set_viewport({ 0.0, 0.0,
                                                 static_cast<float>(vulkan_renderer.swap_chain.get_width()),
                                                 static_cast<float>(vulkan_renderer.swap_chain.get_height()),
                                                 0.0, 1.0 });

            pipeline.fixed_stages.enable_depth_test();
            pipeline.fixed_stages.set_front_face(VK_FRONT_FACE_CLOCKWISE);
            pipeline.fixed_stages.enable_alpha_mix(0);

            std::uint32_t light_count = vulkan_renderer.shadow_maps.size();

            struct Constants {
                std::uint32_t light_size;
            } constant_data {
                light_count
            };

            std::vector<VkSpecializationMapEntry> constants {
                { 0, 0, sizeof(std::uint32_t) } // light size
            };

            pipeline.shader_stages.emplace_back(vulkan_renderer.device, SHADER("volume/volume.vert"));
            vk::DebugMarker::object_name(vulkan_renderer.device, pipeline.shader_stages[0], VK_OBJECT_TYPE_SHADER_MODULE, "Volume Vertex Shader");
            pipeline.shader_stages.emplace_back(vulkan_renderer.device, SHADER("volume/volume.frag"), constants, &constant_data, sizeof(constant_data));
            vk::DebugMarker::object_name(vulkan_renderer.device, pipeline.shader_stages[1], VK_OBJECT_TYPE_SHADER_MODULE, "Volume Fragment Shader");

            std::vector<vk::DescriptorSet::Binding> descriptor_bindings {
                { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
                { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
                { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
                { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER }
            };

            pipeline.descriptor_set_layout = vk::DescriptorSet::Layout { vulkan_renderer.device, descriptor_bindings };

            vk::DebugMarker::object_name(vulkan_renderer.device, pipeline.descriptor_set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "Volume Descriptor Set Layout");

            pipeline.descriptor_sets = vulkan_renderer.descriptor_pool.allocate(vulkan_renderer.swap_chain.size(),
                                                                                pipeline.descriptor_set_layout,
                                                                                "Volume Descriptor Set");

            for (std::size_t i { 0 }; i < pipeline.descriptor_sets.size(); ++i) {
                pipeline.descriptor_sets[i].write(0, vulkan_renderer.camera[i]);
                pipeline.descriptor_sets[i].write(1, vulkan_renderer.lights[i]);
            }

            pipeline.pipeline_layout = vk::Pipeline::Layout {
                vulkan_renderer.device,
                pipeline.descriptor_set_layout,
                {
                    { VK_SHADER_STAGE_ALL, 0, sizeof(glm::mat4) } // model.
                }
            };

            vk::DebugMarker::object_name(vulkan_renderer.device, pipeline.pipeline_layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Volume Pipeline Layout");

            pipeline.pipeline = vk::GraphicsPipeline {
                vulkan_renderer.device,
                pipeline.shader_stages,
                pipeline.fixed_stages,
                pipeline.pipeline_layout,
                vulkan_renderer.color_pass
            };

            vk::DebugMarker::object_name(vulkan_renderer.device, pipeline.pipeline, VK_OBJECT_TYPE_PIPELINE, "Volume Graphics Pipeline");
        }

        int Volume::id { 0 };
    }
}