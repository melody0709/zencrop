import os
from PIL import Image, ImageDraw


def generate_extension_icons():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out_dir = os.path.join(root, "extensions", "browser-companion", "icons")
    ico_path = os.path.join(root, "src", "app.ico")
    os.makedirs(out_dir, exist_ok=True)

    bg_color = (0, 128, 208, 255)
    fg_color = (255, 255, 255, 255)

    # 1. 16x16: Pixel-crisp aligned with smooth rounded rect
    im16 = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    d16 = ImageDraw.Draw(im16)
    d16.rounded_rectangle([0, 0, 15, 15], radius=2, fill=bg_color)
    d16.rectangle([4, 4, 11, 5], fill=fg_color)
    d16.rectangle([4, 10, 11, 11], fill=fg_color)
    d16.point((4, 3), fill=fg_color)
    d16.rectangle([5, 6, 7, 6], fill=fg_color)
    d16.rectangle([6, 7, 8, 7], fill=fg_color)
    d16.rectangle([7, 8, 9, 8], fill=fg_color)
    d16.rectangle([8, 9, 10, 9], fill=fg_color)
    d16.point((11, 12), fill=fg_color)
    im16.save(os.path.join(out_dir, "icon-16.png"))

    # 2. 32x32: Pixel-exact from app.ico with smooth rounded corners
    im_ico = Image.open(ico_path).convert("RGBA")
    mask_hi = Image.new("L", (32 * 8, 32 * 8), 0)
    d_mask = ImageDraw.Draw(mask_hi)
    d_mask.rounded_rectangle([0, 0, 32 * 8 - 1, 32 * 8 - 1], radius=6 * 8, fill=255)
    mask32 = mask_hi.resize((32, 32), Image.Resampling.LANCZOS)

    im32 = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    im32.paste(im_ico, (0, 0), mask32)
    im32.save(os.path.join(out_dir, "icon-32.png"))

    # 3. Vector-based 48x48 and 128x128 with Lanczos antialiasing
    def render_vector(size, radius):
        scale = 8
        canvas = size * scale
        u = canvas / 32.0

        hi = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
        d = ImageDraw.Draw(hi)
        d.rounded_rectangle([0, 0, canvas - 1, canvas - 1], radius=radius * scale, fill=bg_color)

        # Top bar
        d.rectangle([8 * u, 8 * u, 25 * u, 11 * u], fill=fg_color)
        # Bottom bar
        d.rectangle([8 * u, 21 * u, 25 * u, 24 * u], fill=fg_color)
        # Top serif
        d.polygon([(8 * u, 5 * u), (8 * u, 8 * u), (11 * u, 8 * u)], fill=fg_color)
        # Diagonal
        d.polygon([(10 * u, 11 * u), (15 * u, 11 * u), (25 * u, 21 * u), (20 * u, 21 * u)], fill=fg_color)
        # Bottom serif
        d.polygon([(22 * u, 24 * u), (25 * u, 24 * u), (25 * u, 27 * u)], fill=fg_color)

        return hi.resize((size, size), Image.Resampling.LANCZOS)

    im48 = render_vector(48, 9)
    im48.save(os.path.join(out_dir, "icon-48.png"))

    im128 = render_vector(128, 24)
    im128.save(os.path.join(out_dir, "icon-128.png"))

    print("All extension icons generated successfully.")


if __name__ == "__main__":
    generate_extension_icons()
