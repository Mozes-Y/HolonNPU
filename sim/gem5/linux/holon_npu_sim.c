#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/uaccess.h>

#include "holon_npu_abi_internal.h"
#include "holon_npu_sim_uapi.h"

struct holon_npu_sim_device {
    struct device *device;
    void __iomem *registers;
    void *dma_cpu;
    dma_addr_t dma_address;
    size_t dma_bytes;
    void *descriptor_cpu;
    dma_addr_t descriptor_address;
    struct miscdevice misc;
    wait_queue_head_t completion_wait;
    struct mutex submit_lock;
};

static u64 holon_npu_read_counter(
    struct holon_npu_sim_device *npu,
    u32 low_offset,
    u32 high_offset
)
{
    u32 high_before;
    u32 low;
    u32 high_after;

    do {
        high_before = readl(npu->registers + high_offset);
        low = readl(npu->registers + low_offset);
        high_after = readl(npu->registers + high_offset);
    } while (high_before != high_after);
    return ((u64)high_after << 32) | low;
}

static bool holon_npu_buffer_range_valid(
    struct holon_npu_sim_device *npu,
    u64 offset,
    u64 bytes,
    u32 alignment
)
{
    u64 end;

    return alignment != 0 && (offset & (alignment - 1)) == 0 &&
        !check_add_overflow(offset, bytes, &end) && end <= npu->dma_bytes;
}

static int holon_npu_relocate_descriptor(
    struct holon_npu_sim_device *npu,
    u64 descriptor_offset,
    dma_addr_t *descriptor_address
)
{
    const struct holon_npu_program_desc_kernel *source;
    struct holon_npu_program_desc_kernel *descriptor = npu->descriptor_cpu;

    if (!holon_npu_buffer_range_valid(
            npu, descriptor_offset, HOLON_NPU_PROGRAM_DESC_SIZE,
            HOLON_NPU_PROGRAM_DESC_ALIGN))
        return -EINVAL;

    source = npu->dma_cpu + descriptor_offset;
    if (source->size_bytes != HOLON_NPU_PROGRAM_DESC_SIZE ||
        source->version != HOLON_NPU_ABI_MAJOR ||
        source->program_format != HOLON_NPU_PROGRAM_FORMAT_HOLON ||
        !holon_npu_buffer_range_valid(
            npu, source->code_addr, source->code_size_bytes,
            HOLON_NPU_PROGRAM_IMAGE_ALIGN) ||
        !holon_npu_buffer_range_valid(
            npu, source->arg_addr, source->arg_size_bytes,
            HOLON_NPU_PROGRAM_ARGUMENT_ALIGN) ||
        (source->completion_addr != 0 &&
         !holon_npu_buffer_range_valid(
             npu, source->completion_addr, 32,
             HOLON_NPU_PROGRAM_COMPLETION_ALIGN)))
        return -EINVAL;

    memcpy(descriptor, source, sizeof(*descriptor));
    descriptor->code_addr += npu->dma_address;
    descriptor->arg_addr += npu->dma_address;
    if (descriptor->completion_addr != 0)
        descriptor->completion_addr += npu->dma_address;
    dma_wmb();
    *descriptor_address = npu->descriptor_address;
    return 0;
}

static irqreturn_t holon_npu_irq(int irq, void *data)
{
    struct holon_npu_sim_device *npu = data;
    u32 pending = readl(npu->registers + HOLON_NPU_REG_IRQ_STATUS);

    if (pending == 0)
        return IRQ_NONE;
    writel(pending, npu->registers + HOLON_NPU_REG_IRQ_CLEAR);
    wake_up_interruptible(&npu->completion_wait);
    return IRQ_HANDLED;
}

static int holon_npu_open(struct inode *inode, struct file *file)
{
    struct miscdevice *misc = file->private_data;
    struct holon_npu_sim_device *npu =
        container_of(misc, struct holon_npu_sim_device, misc);

    file->private_data = npu;
    return nonseekable_open(inode, file);
}

static ssize_t holon_npu_read(
    struct file *file,
    char __user *buffer,
    size_t bytes,
    loff_t *position
)
{
    struct holon_npu_sim_device *npu = file->private_data;
    struct holon_npu_sim_snapshot snapshot;

    if (bytes < sizeof(snapshot))
        return -EINVAL;

    dma_rmb();
    snapshot.status = readl(npu->registers + HOLON_NPU_REG_STATUS);
    snapshot.fault_code = readl(npu->registers + HOLON_NPU_REG_FAULT_CODE);
    snapshot.debug_pc = readl(npu->registers + HOLON_NPU_REG_DEBUG_PC);
    snapshot.irq_status = readl(npu->registers + HOLON_NPU_REG_IRQ_STATUS);
    snapshot.cycles = holon_npu_read_counter(
        npu, HOLON_NPU_REG_PERF_CYCLE_LO, HOLON_NPU_REG_PERF_CYCLE_HI);
    snapshot.instructions_retired = holon_npu_read_counter(
        npu, HOLON_NPU_REG_PERF_INSTRET_LO, HOLON_NPU_REG_PERF_INSTRET_HI);
    snapshot.dma_address = npu->dma_address;
    snapshot.dma_bytes = npu->dma_bytes;
    if (copy_to_user(buffer, &snapshot, sizeof(snapshot)))
        return -EFAULT;
    return sizeof(snapshot);
}

static ssize_t holon_npu_write(
    struct file *file,
    const char __user *buffer,
    size_t bytes,
    loff_t *position
)
{
    struct holon_npu_sim_device *npu = file->private_data;
    dma_addr_t descriptor_address;
    u64 descriptor_offset;
    u32 status;
    int error;

    if (bytes != sizeof(descriptor_offset))
        return -EINVAL;
    if (copy_from_user(&descriptor_offset, buffer, sizeof(descriptor_offset)))
        return -EFAULT;
    if (mutex_lock_interruptible(&npu->submit_lock))
        return -ERESTARTSYS;

    status = readl(npu->registers + HOLON_NPU_REG_STATUS);
    if (!(status & HOLON_NPU_STATUS_IDLE)) {
        error = -EBUSY;
        goto unlock;
    }
    error = holon_npu_relocate_descriptor(
        npu, descriptor_offset, &descriptor_address);
    if (error)
        goto unlock;

    writel(HOLON_NPU_IRQ_DONE | HOLON_NPU_IRQ_FAULT,
           npu->registers + HOLON_NPU_REG_IRQ_ENABLE);
    writel(lower_32_bits(descriptor_address),
           npu->registers + HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO);
    writel(upper_32_bits(descriptor_address),
           npu->registers + HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI);
    writel(HOLON_NPU_DOORBELL_START, npu->registers + HOLON_NPU_REG_DOORBELL);
    error = bytes;

unlock:
    mutex_unlock(&npu->submit_lock);
    return error;
}

static __poll_t holon_npu_poll(struct file *file, poll_table *wait)
{
    struct holon_npu_sim_device *npu = file->private_data;
    u32 status;

    poll_wait(file, &npu->completion_wait, wait);
    status = readl(npu->registers + HOLON_NPU_REG_STATUS);
    if (status & (HOLON_NPU_STATUS_DONE | HOLON_NPU_STATUS_FAULT))
        return EPOLLIN | EPOLLRDNORM;
    return 0;
}

static int holon_npu_mmap(struct file *file, struct vm_area_struct *area)
{
    struct holon_npu_sim_device *npu = file->private_data;
    size_t bytes = area->vm_end - area->vm_start;

    if (area->vm_pgoff != 0 || bytes > npu->dma_bytes)
        return -EINVAL;
    return dma_mmap_coherent(
        npu->device, area, npu->dma_cpu, npu->dma_address, npu->dma_bytes);
}

static const struct file_operations holon_npu_file_operations = {
    .owner = THIS_MODULE,
    .open = holon_npu_open,
    .read = holon_npu_read,
    .write = holon_npu_write,
    .poll = holon_npu_poll,
    .mmap = holon_npu_mmap,
    .llseek = no_llseek,
};

static int holon_npu_probe(struct platform_device *platform_device)
{
    struct holon_npu_sim_device *npu;
    int irq;
    int error;

    npu = devm_kzalloc(&platform_device->dev, sizeof(*npu), GFP_KERNEL);
    if (!npu)
        return -ENOMEM;
    npu->device = &platform_device->dev;
    npu->registers = devm_platform_ioremap_resource(platform_device, 0);
    if (IS_ERR(npu->registers))
        return PTR_ERR(npu->registers);
    error = dma_set_mask_and_coherent(npu->device, DMA_BIT_MASK(64));
    if (error)
        return error;

    npu->dma_bytes = HOLON_NPU_SIM_DMA_BUFFER_BYTES;
    npu->dma_cpu = dma_alloc_coherent(
        npu->device, npu->dma_bytes, &npu->dma_address, GFP_KERNEL);
    if (!npu->dma_cpu)
        return -ENOMEM;
    npu->descriptor_cpu = dma_alloc_coherent(
        npu->device, HOLON_NPU_PROGRAM_DESC_SIZE, &npu->descriptor_address,
        GFP_KERNEL);
    if (!npu->descriptor_cpu) {
        error = -ENOMEM;
        goto free_dma;
    }
    init_waitqueue_head(&npu->completion_wait);
    mutex_init(&npu->submit_lock);

    irq = platform_get_irq(platform_device, 0);
    if (irq < 0) {
        error = irq;
        goto free_descriptor;
    }
    error = devm_request_irq(npu->device, irq, holon_npu_irq, 0,
                             dev_name(npu->device), npu);
    if (error)
        goto free_descriptor;

    npu->misc.minor = MISC_DYNAMIC_MINOR;
    npu->misc.name = "holonnpu-sim";
    npu->misc.fops = &holon_npu_file_operations;
    npu->misc.parent = npu->device;
    error = misc_register(&npu->misc);
    if (error)
        goto free_descriptor;

    platform_set_drvdata(platform_device, npu);
    dev_info(npu->device, "HolonNPU simulation device ready\n");
    return 0;

free_descriptor:
    dma_free_coherent(npu->device, HOLON_NPU_PROGRAM_DESC_SIZE,
                      npu->descriptor_cpu, npu->descriptor_address);
free_dma:
    dma_free_coherent(npu->device, npu->dma_bytes,
                      npu->dma_cpu, npu->dma_address);
    return error;
}

static void holon_npu_remove(struct platform_device *platform_device)
{
    struct holon_npu_sim_device *npu = platform_get_drvdata(platform_device);
    u32 status;

    writel(HOLON_NPU_CONTROL_SOFT_RESET, npu->registers + HOLON_NPU_REG_CONTROL);
    if (readl_poll_timeout(
            npu->registers + HOLON_NPU_REG_STATUS,
            status,
            status & HOLON_NPU_STATUS_IDLE,
            10,
            1000000))
        dev_warn(npu->device, "timed out waiting for soft reset\n");
    misc_deregister(&npu->misc);
    dma_free_coherent(npu->device, HOLON_NPU_PROGRAM_DESC_SIZE,
                      npu->descriptor_cpu, npu->descriptor_address);
    dma_free_coherent(npu->device, npu->dma_bytes,
                      npu->dma_cpu, npu->dma_address);
}

static const struct of_device_id holon_npu_of_match[] = {
    { .compatible = "holonnpu,sim-3.0" },
    { }
};
MODULE_DEVICE_TABLE(of, holon_npu_of_match);

static struct platform_driver holon_npu_platform_driver = {
    .probe = holon_npu_probe,
    .remove_new = holon_npu_remove,
    .driver = {
        .name = "holonnpu-sim",
        .of_match_table = holon_npu_of_match,
    },
};
module_platform_driver(holon_npu_platform_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HolonNPU gem5 simulation platform driver");
