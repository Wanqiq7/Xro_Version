# BlinkLED

控制 LED 闪烁的简单模块 / A simple module to control LED blinking

## 模块简介 / Module Description

BlinkLED 是一个用于演示和控制 LED 闪烁的简单模块。
A simple module for controlling LED blinking.

## 硬件需求 / Required Hardware

* 支持的 LED 设备节点名称之一（下列任一名称即可）：
* One of the following LED device node names is required.
  * `led`
  * `LED`
  * `led1`
  * `LED1`


## 构造参数 / Constructor Arguments

* `blink_cycle` (默认值: 250)

  * LED 闪烁周期，单位为毫秒。
  * The blinking cycle in milliseconds (default: 250).

## 依赖 / Depends

* 无依赖（除 LibXR 基础框架外）。
* No dependencies (except LibXR core framework).

## 错误处理 / Error Handling

* 发生致命错误时，模块会停止定时器，并通过特殊闪烁方式提示异常（125ms/125ms/500ms/500ms 闪烁）。
* On fatal error, the timer is stopped and the LED will blink in a special pattern (125ms/125ms/500ms/500ms) to indicate fault.
