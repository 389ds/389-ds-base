import js from "@eslint/js";
import { fixupPluginRules } from "@eslint/compat";
import globals from "globals";
import reactPlugin from "eslint-plugin-react";
import reactHooksPlugin from "eslint-plugin-react-hooks";

export default [
    // Global ignores (replaces .eslintignore)
    {
        ignores: ["node_modules/*", "pkg/lib/*"],
    },

    // Base recommended rules
    js.configs.recommended,

    // Main config for JS/JSX files
    {
        files: ["src/**/*.{js,jsx}"],
        plugins: {
            react: fixupPluginRules(reactPlugin),
            "react-hooks": reactHooksPlugin,
        },
        languageOptions: {
            ecmaVersion: 2022,
            sourceType: "module",
            globals: {
                ...globals.browser,
                require: "readonly",
                module: "readonly",
            },
            parserOptions: {
                ecmaFeatures: {
                    jsx: true,
                },
            },
        },
        settings: {
            react: {
                version: "detect",
            },
        },
        rules: {
            // Indentation
            indent: [
                "error",
                4,
                {
                    ObjectExpression: "first",
                    CallExpression: { arguments: "first" },
                    MemberExpression: 2,
                    ignoredNodes: ["JSXAttribute"],
                },
            ],

            // Code style
            "newline-per-chained-call": [
                "error",
                { ignoreChainWithDepth: 2 },
            ],
            "no-var": "error",
            "lines-between-class-members": [
                "error",
                "always",
                { exceptAfterSingleLine: true },
            ],
            "prefer-promise-reject-errors": [
                "error",
                { allowEmptyReject: true },
            ],
            semi: ["error", "always", { omitLastInOneLineBlock: true }],

            // React
            "react/jsx-indent": ["error", 4],
            "react-hooks/rules-of-hooks": "error",
            "react-hooks/exhaustive-deps": "error",

            // Disabled rules (were from standard configs, not needed)
            camelcase: "off",
            "comma-dangle": "off",
            curly: "off",
            "jsx-quotes": "off",
            "key-spacing": "off",
            "no-console": "off",
            quotes: "off",
            "react/jsx-curly-spacing": "off",
            "react/jsx-indent-props": "off",
            "react/prop-types": "off",
            "space-before-function-paren": "off",
        },
    },
];
