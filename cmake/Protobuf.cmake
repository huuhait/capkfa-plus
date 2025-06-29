function(generate_proto_grpc TARGET_NAME PROTO_FILES OUT_SRCS OUT_HDRS)
    # Arguments:
    #   TARGET_NAME - name of the target that will depend on generated files (for dependencies)
    #   PROTO_FILES - list of proto files to generate from
    #   OUT_SRCS    - output variable name to hold generated .cc files
    #   OUT_HDRS    - output variable name to hold generated .h files

    set(generated_srcs "")
    set(generated_hdrs "")

    foreach(proto_file IN LISTS PROTO_FILES)
        get_filename_component(proto_name ${proto_file} NAME_WE)
        set(GENERATED_DIR "${CMAKE_BINARY_DIR}/proto")

        # Ensure output directory exists
        file(MAKE_DIRECTORY ${GENERATED_DIR})

        # Protobuf output files
        set(proto_cc "${GENERATED_DIR}/${proto_name}.pb.cc")
        set(proto_h "${GENERATED_DIR}/${proto_name}.pb.h")

        # gRPC output files
        set(grpc_cc "${GENERATED_DIR}/${proto_name}.grpc.pb.cc")
        set(grpc_h "${GENERATED_DIR}/${proto_name}.grpc.pb.h")

        # Protobuf generation command
        add_custom_command(
                OUTPUT ${proto_cc} ${proto_h}
                COMMAND ${PROTOC_EXECUTABLE}
                ARGS --cpp_out=${GENERATED_DIR}
                -I ${CMAKE_SOURCE_DIR}/proto
                ${proto_file}
                DEPENDS ${proto_file}
                COMMENT "Generating protobuf sources for ${proto_file}"
        )

        # gRPC generation command
        add_custom_command(
                OUTPUT ${grpc_cc} ${grpc_h}
                COMMAND ${PROTOC_EXECUTABLE}
                ARGS --grpc_out=${GENERATED_DIR}
                --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                -I ${CMAKE_SOURCE_DIR}/proto
                ${proto_file}
                DEPENDS ${proto_file}
                COMMENT "Generating gRPC sources for ${proto_file}"
        )

        # Append generated files to lists
        list(APPEND generated_srcs ${proto_cc} ${grpc_cc})
        list(APPEND generated_hdrs ${proto_h} ${grpc_h})
    endforeach()

    # Create a custom target to build all generated files
    add_custom_target(${TARGET_NAME}_proto ALL DEPENDS ${generated_srcs} ${generated_hdrs})

    # Set output variables
    set(${OUT_SRCS} ${generated_srcs} PARENT_SCOPE)
    set(${OUT_HDRS} ${generated_hdrs} PARENT_SCOPE)
endfunction()
