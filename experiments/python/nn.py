import numpy as np
from mem_pattern_trace import *
import torch
import torchvision
import torchvision.models as models
from PIL import Image
from torchvision import transforms

torch.set_num_threads(1)

preprocess = transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(224),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
])
alexnet = models.alexnet(pretrained=True)

syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO)
input_image = Image.open("dog.jpg")

input_tensor = preprocess(input_image)
input_batch = input_tensor.unsqueeze(0) # create a mini-batch as expected by the model
with torch.no_grad():
        output = alexnet(torch.rand([1,3,244,244]))

syscall(mem_pattern_trace, TRACE_END)

print(np.argmax(torch.nn.functional.softmax(output[0], dim=0)))
