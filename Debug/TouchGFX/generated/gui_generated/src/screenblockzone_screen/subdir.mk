################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../TouchGFX/generated/gui_generated/src/screenblockzone_screen/ScreenBlockZoneViewBase.cpp 

OBJS += \
./TouchGFX/generated/gui_generated/src/screenblockzone_screen/ScreenBlockZoneViewBase.o 

CPP_DEPS += \
./TouchGFX/generated/gui_generated/src/screenblockzone_screen/ScreenBlockZoneViewBase.d 


# Each subdirectory must supply rules for building sources it contributes
TouchGFX/generated/gui_generated/src/screenblockzone_screen/%.o TouchGFX/generated/gui_generated/src/screenblockzone_screen/%.su TouchGFX/generated/gui_generated/src/screenblockzone_screen/%.cyclo: ../TouchGFX/generated/gui_generated/src/screenblockzone_screen/%.cpp TouchGFX/generated/gui_generated/src/screenblockzone_screen/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m33 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32H523xx -c -I../Core/Inc -I"D:/work/stm_workspace/device_lib/include" -I../Drivers/STM32H5xx_HAL_Driver/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H5xx/Include -I../Drivers/CMSIS/Include -I../TouchGFX/App -I../TouchGFX/target/generated -I../TouchGFX/target -I../Middlewares/ST/touchgfx/framework/include -I../TouchGFX/generated/fonts/include -I../TouchGFX/generated/gui_generated/include -I../TouchGFX/generated/images/include -I../TouchGFX/generated/texts/include -I../TouchGFX/generated/videos/include -I../TouchGFX/gui/include -I"D:/work/stm_workspace/device_lib" -I/device_lib -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-TouchGFX-2f-generated-2f-gui_generated-2f-src-2f-screenblockzone_screen

clean-TouchGFX-2f-generated-2f-gui_generated-2f-src-2f-screenblockzone_screen:
	-$(RM) ./TouchGFX/generated/gui_generated/src/screenblockzone_screen/ScreenBlockZoneViewBase.cyclo ./TouchGFX/generated/gui_generated/src/screenblockzone_screen/ScreenBlockZoneViewBase.d ./TouchGFX/generated/gui_generated/src/screenblockzone_screen/ScreenBlockZoneViewBase.o ./TouchGFX/generated/gui_generated/src/screenblockzone_screen/ScreenBlockZoneViewBase.su

.PHONY: clean-TouchGFX-2f-generated-2f-gui_generated-2f-src-2f-screenblockzone_screen

