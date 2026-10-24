#pragma once
#include "../model/controller_pictures.hpp"
#include <d2d1helper.h>

// Shared by the panel and connection toast. Official 8BitDo PNGs are strips
// with side/front/back views; show only the middle, front-facing controller.
inline D2D1_RECT_F controllerPictureSource(devices::Picture picture, D2D1_SIZE_F size, D2D1_RECT_F ink) {
    if (const auto* photo = devices::controllerPhoto(picture); photo && (photo->left != 0 || photo->right != 1))
        return D2D1::RectF(size.width * photo->left, 0, size.width * photo->right, size.height);
    return ink;
}
