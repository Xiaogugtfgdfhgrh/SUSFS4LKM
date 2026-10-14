import { defineConfig } from 'vite'
import { viteStaticCopy } from 'vite-plugin-static-copy'
export default defineConfig({
    build: {
        target: 'esnext',
        supported: {
            'top-level-await': true
        },
        rollupOptions: {
            input: {
                main: './index.html',
                credits: './credits.html',
                custom: './custom.html',
                status: './status.html',
                advanced: './advanced.html',
                fade: './fade.js',
                i18n: './i18n.js',
            }
        }
    },
    esbuild: {
      supported: {
        'top-level-await': true
      },
    },
    assetsInclude: ['**/*.ttf', '**/*.otf', '**/*.woff', '**/*.woff2','**/*.xml'],
    plugins: [
        viteStaticCopy({
            targets: [
                {
                    src: 'languages/*',
                    dest: 'languages'
                },
                {
                    src: 'config.json',
                    dest: ''
                }
            ]
        })
    ]
})
