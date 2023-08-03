from typing import List

from ..common import IndentedBuffer
from ..triton import TritonScheduling
from ... import config
from ...codecache import code_hash, get_path
from ...scheduler import BaseSchedulerNode
from ...utils import get_fused_kernel_name, get_kernel_metadata
from ...virtualized import V


class CUDAScheduling(TritonScheduling):
    def define_kernel(self, src_code, node_schedule):
        wrapper = V.graph.wrapper_code
        if src_code in wrapper.src_to_kernel:
            kernel_name = wrapper.src_to_kernel[src_code]
        else:
            fused_name = (
                get_fused_kernel_name(node_schedule, config.triton.descriptive_names)
                if config.triton.descriptive_names
                else ""
            )
            kernel_name = "_".join(
                ["cuda", fused_name, wrapper.next_kernel_suffix()]
            )
            # use the original src_code as the key
            wrapper.src_to_kernel[src_code] = kernel_name
            src_code = src_code.replace("KERNEL_NAME", kernel_name)

            basename, _, kernel_path = get_path(code_hash(src_code), "py")

            compile_wrapper = IndentedBuffer()
            compile_wrapper.writeline(f"async_compile.cuda('so', '''")
            compile_wrapper.splice(src_code, strip=True)
            compile_wrapper.writeline("''')")

            metadata_comment = f"# kernel path: {kernel_path}"
            origins, detailed_origins = get_kernel_metadata(node_schedule, wrapper)
            metadata_comment += "\n" + origins + "\n" + detailed_origins
            wrapper.define_kernel(
                kernel_name, compile_wrapper.getvalue(), metadata_comment
            )
        return kernel_name
